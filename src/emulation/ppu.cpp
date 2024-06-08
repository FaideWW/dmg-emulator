#include "emulator.h"

#define LINE_DOTS 456
#define MAX_LINES 154
#define VBLANK_LINE_START 144

#define MODE2_DOTS 80
#define MODE3_DOTS_MIN 172
#define MODE3_DOTS_MAX 289
#define MODE0_DOTS_MIN 87
#define MODE0_DOTS_MAX 204

int cmp(const void *a, const void *b) {
  return ((oam_sprite *)a)->x - ((oam_sprite *)b)->x;
}

bool checkStatInterrupt(emulator_state *state) {
  uint8_t stat;
  internal_readMemory8(state, MEM_STAT, &stat);

  // check if mode 0 condition is met
  bool mode0IntEnabled = checkBit(stat, STAT_SELECT_MODE0);
  bool mode1IntEnabled = checkBit(stat, STAT_SELECT_MODE1);
  bool mode2IntEnabled = checkBit(stat, STAT_SELECT_MODE2);
  bool lycIntEnabled = checkBit(stat, STAT_SELECT_LYC);
  bool lycCondMet = checkBit(stat, STAT_LY_LYC);

  uint8_t ppuMode = readPPUMode(state);
  return ((ppuMode == 0 && mode0IntEnabled) ||
          (ppuMode == 1 && mode1IntEnabled) ||
          (ppuMode == 2 && mode2IntEnabled) || (lycCondMet && lycIntEnabled));
}

// at the current scanline, scan OAM memory and find any sprites that will be
// rendered to this line (up to a maximum of 10)
void doOAMScan(emulator_state *state) {
  dmg_ppu *ppu = &state->ppu;
  uint8_t ly, lcdc;
  internal_readMemory8(state, MEM_LY, &ly);
  internal_readMemory8(state, MEM_LCDC, &lcdc);

  // account for the scroll gap at the top of the screen (16 pixels)
  uint8_t screenY = ly + 16;
  bool useTallSprites = checkBit(lcdc, 2);

  uint16_t addr = MEM_OAM;
  ppu->oamSpriteHead = 0;

  size_t spriteCount = 0;
  while (addr < MEM_OAM + MEM_OAM_SIZE && spriteCount < 10) {
    oam_sprite sprite;
    internal_readMemory8(state, addr++, &sprite.y);
    internal_readMemory8(state, addr++, &sprite.x);
    internal_readMemory8(state, addr++, &sprite.tileNum);
    internal_readMemory8(state, addr++, &sprite.flags);

    if (sprite.x > 0 && screenY >= sprite.y &&
        screenY < (sprite.y + (useTallSprites ? 8 : 16))) {
      ppu->oamSpriteBuffer[spriteCount++] = sprite;
    }
  }
  ppu->spritesInBuffer = spriteCount;

  /* debugLog.AddLog("[PPU] OAM scan - found %d sprites on LY=%d\n", */
  /*                 (int)spriteCount, ly); */

  qsort(ppu->oamSpriteBuffer, spriteCount, sizeof(oam_sprite), cmp);
}

// performs a background/window pixel fetch, and returns the number of cycles
// taken to do so
int doBGPixelFetch(emulator_state *state, fifo_pixel *fifo) {
  dmg_ppu *ppu = &state->ppu;
  uint8_t ly, lcdc;
  internal_readMemory8(state, MEM_LY, &ly);
  internal_readMemory8(state, MEM_LCDC, &lcdc);

  bool bgWindowEnable = checkBit(lcdc, 0);
  bool bgTileSelect = checkBit(lcdc, 3);
  bool tileDataSelect = checkBit(lcdc, 4);
  bool windowEnable = checkBit(lcdc, 5);
  bool windowTileSelect = checkBit(lcdc, 6);

  debugLog.AddLog("[PPU] doing bg pixel fetch at tile %d, ly=%d, dot=%d\n",
                  ppu->internalXCounter, ly, ppu->currentDot);

  int cycles = 0;

  // if BG and window display are turned off, push all white pixels to the
  // queue
  if (!bgWindowEnable) {
    for (int i = 0; i < 8; i++) {
      fifo[i].pixelSrc = BG_PIXEL;
      fifo[i].pixelColor = 0;
    }
    return 6;
  }

  uint8_t scx, scy;
  internal_readMemory8(state, MEM_SCX, &scx);
  internal_readMemory8(state, MEM_SCY, &scy);

  // step 1: get tile
  //
  // decode the background or window tile id to fetch based on the tilemap, the
  // scroll registers, and where the ppu is on the screen
  uint8_t tileNum;
  uint8_t tileX = ppu->internalXCounter;
  uint16_t baseAddr, tileOffset;
  if (ppu->isFetchingWindow && windowEnable) {
    // window tile map 0 is at $9800, window tile map 1 is at $9C00;
    baseAddr = (windowTileSelect ? 0x9C00 : 0x9800);
    // the window has its own internal line counter that increments at the same
    // rate as LY, but only when we're inside the window
    uint8_t pY = ppu->windowLineCounter;

    // 32 tiles per row, 8 lines per tile
    tileOffset = 32 * (pY / 8) + tileX;

  } else {
    // bg tile map 0 is at $9800, bg tile map 1 is at $9C00;
    baseAddr = (bgTileSelect ? 0x9C00 : 0x9800);

    // in the full bg map (32 tiles in height), pixelY can be a value between
    // 0-255 (represnting the current "scanline" in the bg tile map)
    uint8_t pixelY = ((ly + scy) & 0xFF);
    // (pixelY / 8 lines per tile) * 32 tiles per row
    uint16_t yOffset = 32 * (pixelY / 8);

    // (scx / 8) ensures we land on a tile boundary (the fetcher always fetches
    // 8 pixels, or one tile's width). then the whole thing is ANDed with 0x1F
    // to ensure we only capture the lower 5 bits (0-31), and will wrap back
    // around to 0 if needed
    uint16_t xOffset = ((tileX + (scx / 8)) & 0x1F);
    tileOffset = yOffset + xOffset;
    /* debugLog.AddLog( */
    /*     "[PPU] fetching tile (baseaddr:%04hX, tileX:%d, pixelY:%d, ",
     * baseAddr, */
    /*     tileX, pixelY); */
  }
  // clamp tile offset so we don't exceed VRAM
  uint16_t tileAddr = baseAddr + (tileOffset & 0x03FF);

  internal_readMemory8(state, tileAddr, &tileNum);
  /* debugLog.AddLog("tileAddr:%04hX, tileNum:%02X)", tileAddr, tileNum); */

  cycles += 2;

  // step 2: fetch tile data (low)
  //
  // fetch the low byte of the tile identified in step 1 from VRAM
  // tileDataSelect determines whether we use 8000 or 8800 as a base address
  uint16_t vramBaseAddr = tileDataSelect ? 0x8000 : 0x8800;

  // read into the tile data to get the correct byte for the scanline we're
  // about to push
  uint16_t byteOffset = ppu->isFetchingWindow
                            ? (2 * (ppu->windowLineCounter % 8))
                            : (2 * ((ly + scy) % 8));

  // the tile numbers are unsigned in 8000 addressing mode, and signed in 8800
  // addressing mode
  uint16_t loAddr = vramBaseAddr +
                    ((tileDataSelect ? tileNum : (int8_t)tileNum) * 16) +
                    byteOffset;

  uint8_t loByte;
  internal_readMemory8(state, loAddr, &loByte);
  cycles += 2;

  // step 3: fetch tile data (high)
  //
  // fetch the high byte of the tile identified in step 1
  // (same as step 2, but +1 to get the high byte)
  uint8_t hiByte;
  internal_readMemory8(state, loAddr + 1, &hiByte);
  cycles += 2;

  /* debugLog.AddLog(" vram addr:%04hX (tile bytes: %02X %02X) ", loAddr,
   * loByte, */
  /*                 hiByte); */

  // step 4: push pixels
  //
  // decode the tile bytes into pixel data, encode the relevant data, and push
  // to the fifo
  /* debugLog.AddLog(" pixels: "); */
  for (int i = 0; i < 8; i++) {
    // following the 2BPP format, we combine the ith bit of the high byte with
    // the ith bit of the low byte to get the 2-bit color value for the pixel
    uint8_t pixel = 0;
    pixel |= (loByte >> i) & 0x1;
    pixel |= ((hiByte >> i) & 0x1) << 1;

    // the following are disabled for background/window pixels:
    // palette (objs only on DMG)
    // sprite priority (CGB only)
    // background priority (objs only)

    // we then fill the buffer in reverse, since bitwise operations go from
    // right to left, but array indexing goes from left to right
    fifo[7 - i].pixelSrc = BG_PIXEL;
    fifo[7 - i].pixelColor = pixel;

    /* debugLog.AddLog("%02X ", pixel); */
  }

  /* debugLog.AddLog("\n"); */

  // a full fetch takes 6 cycles
  return cycles;
}

// performs a sprite pixel fetch, and returns the number of cycles taken to
// do so
int doSpritePixelFetch(emulator_state *state, fifo_pixel *fifo,
                       oam_sprite sprite) {
  uint8_t ly, lcdc, scx, scy;
  internal_readMemory8(state, MEM_LY, &ly);
  internal_readMemory8(state, MEM_LCDC, &lcdc);
  internal_readMemory8(state, MEM_SCX, &scx);
  internal_readMemory8(state, MEM_SCY, &scy);

  bool useTallSprites = checkBit(lcdc, 2);
  bool objPalette = checkBit(sprite.flags, 4);
  bool flipX = checkBit(sprite.flags, 5);
  bool flipY = checkBit(sprite.flags, 6);

  int cycles = 0;

  // step 1: get tile number (this is passed in as `sprite`)
  cycles += 2;

  // step 2: fetch tile data (low)
  //
  // fetch the low byte of the tile identified in step 1 from VRAM
  uint16_t vramBaseAddr = 0x8000;

  // read into the tile data to get the correct byte for the scanline we're
  // about to push
  uint16_t byteOffset = 2 * ((ly + scy) % 8);

  if (flipY) {
    byteOffset = ((useTallSprites ? 16 : 8) * 2) - byteOffset;
  }

  // sprites are always addressed using 8000
  uint16_t loAddr = vramBaseAddr + (sprite.tileNum * 16) + byteOffset;

  uint8_t loByte;
  internal_readMemory8(state, loAddr, &loByte);
  cycles += 2;

  // step 3: fetch tile data (high)
  //
  // fetch the high byte of the tile identified in step 1
  // (same as step 2, but +1 to get the high byte)
  uint8_t hiByte;
  internal_readMemory8(state, loAddr + 1, &hiByte);
  cycles += 2;

  // step 4: push pixels
  //
  // decode the tile bytes into pixel data, encode the relevant data, and push
  // to the fifo
  for (int i = 0; i < 8; i++) {
    // special case for OAM pixels: if the fifo already has a pixel at this
    // position, it takes precedence over a new pixel. This is true because
    // sprites with lower X values have priority, and our fetcher always fetches
    // sprites from right to left.
    if (state->ppu.pixelsInOamFifo < i) {
      // folloinwg the 2BPP format, we combine the ith bit of the high byte with
      // the ith bit of the low byte to get the 2-bit color value for the pixel
      uint8_t pixel = 0;
      pixel |= (loByte & (1 << i));
      pixel |= (hiByte & (1 << i)) << 1;
      int fifoI = (flipX ? 8 - i : i);

      fifo[fifoI].pixelSrc = OBJ_PIXEL;
      fifo[fifoI].palette = objPalette;
      fifo[fifoI].pixelColor = pixel;

      state->ppu.pixelsInOamFifo += 1;
    }
  }

  // a full fetch takes 6 cycles
  return cycles;
}

void mixPixel(fifo_pixel *bg, fifo_pixel *oam) {
  if (bg->pixelSrc == BG_PIXEL) {
    // sprite pixels can only overwrite bg pixels (obj pixels cannot be
    // overwritten)
    if (oam->bgPriority && bg->pixelColor == 0b00) {
      // if bgPriority is true, the sprite pixel can only overwrite the bg
      // pixel if the bg pixel's color is 00
      bg->pixelColor = oam->pixelColor;
      bg->pixelSrc = oam->pixelSrc;
    } else if (!oam->bgPriority && oam->pixelColor != 0b00) {
      // otherwise, as long as the sprite's color is not 00, it will overwrite
      // the bg pixel
      bg->pixelColor = oam->pixelColor;
      bg->pixelSrc = oam->pixelSrc;
    }
  }
}

/* void mixPixels(fifo_pixel *mainFifo, fifo_pixel *spriteFifo, size_t numToMix,
 */
/*                bool bgPriority) { */
/*   for (int i = 0; i < numToMix; i++) { */
/*     fifo_pixel bg = mainFifo[i]; */
/*     fifo_pixel sprite = spriteFifo[i]; */
/*     if (bg.pixelSrc == BG_PIXEL) { */
/*       // sprite pixels can only overwrite bg pixels (obj pixels cannot be */
/*       // overwritten) */
/*       if (bgPriority && bg.pixelColor == 0b00) { */
/*         // if bgPriority is true, the sprite pixel can only overwrite the bg
 */
/*         // pixel if the bg pixel's color is 00 */
/*         mainFifo[i] = sprite; */
/*       } else if (!bgPriority && sprite.pixelColor != 0b00) { */
/*         // otherwise, as long as the sprite's color is not 00, it will
 * overwrite */
/*         // the bg pixel */
/*         mainFifo[i] = sprite; */
/*       } */
/*     } */
/*   } */
/* } */

void transferPixel(emulator_state *state, uint32_t *graphicsBuffer, uint8_t x,
                   uint8_t y, fifo_pixel pixel) {
  uint16_t paletteAddr;
  if (pixel.pixelSrc == BG_PIXEL) {
    paletteAddr = MEM_BGP;
  } else {
    paletteAddr = (pixel.palette ? MEM_OBP1 : MEM_OBP0);
  }
  uint8_t palette;
  internal_readMemory8(state, paletteAddr, &palette);

  uint8_t color = (palette >> (pixel.pixelColor * 2)) & 0x3;

  /* if (pixel.pixelColor != 0) { */
  /*   debugLog.AddLog( */
  /*       "[PPU] pixel color: %02X, palette: %02X, palette color: %02X\n", */
  /*       pixel.pixelColor, palette, color); */
  /* } */

  uint8_t bufferColor = 0;
  switch (color) {
  case 0b00: {
    bufferColor = COLORDATA_WHITE;
    break;
  }
  case 0b01: {
    /* debugLog.AddLog("[PPU] x: %d, y: %d - lightgrey (%02X)\n", x, y, color);
     */
    bufferColor = COLORDATA_LIGHTGREY;
    break;
  }
  case 0b10: {
    /* debugLog.AddLog("[PPU] x: %d, y: %d - darkgrey (%02X)\n", x, y, color);
     */
    bufferColor = COLORDATA_DARKGREY;
    break;
  }
  case 0b11: {
    /* debugLog.AddLog("[PPU] x: %d, y: %d - black (%02X)\n", x, y, color); */
    bufferColor = COLORDATA_BLACK;
    break;
  }
  }

  /* debugLog.AddLog("[PPU] transferring pixel with color %02X at pos %d,%d\n",
   */
  /*                 bufferColor, x, y); */

  uint32_t *bufferPixel = graphicsBuffer + (y * EMULATOR_SCREEN_WIDTH) + x;

  // write the rgb value to the graphics buffer
  uint8_t a = 0xFF;

  *bufferPixel =
      a << 24 | (bufferColor << 16) | (bufferColor << 8) | (bufferColor << 0);
}

// clocks the PPU by the designated number of cycles. this may advance the PPU
// further than the number of cycles listed (in the case that an operation is
// started with fewer than the required amount of cycles remaining). in this
// case, the cycle "overrun" is stored in ppu->cycleOverrun, and that number of
// cycles will be skipped on future invocatinos
void advancePPU(emulator_state *state, int cyclesToAdvance,
                uint32_t *graphicsBuffer) {
  dmg_ppu *ppu = &state->ppu;

  static int dotsPerMode = 0;
  static int dotsPerLine = 0;
  static int dotsPerFrame = 0;

  // read LCDC
  uint8_t lcdc;
  internal_readMemory8(state, MEM_LCDC, &lcdc);

  bool windowEnable = checkBit(lcdc, 5);
  bool lcdEnabled = checkBit(lcdc, 7);

  // read LY
  uint8_t ly;
  internal_readMemory8(state, MEM_LY, &ly);

  int cycles = 0;
  // if we overran our cycle count last time, wait that amount of cycles
  // before proceeding
  cycles += ppu->cycleOverrun;

  if (lcdEnabled) {
    while (cycles < cyclesToAdvance) {
      int cyclesThisStep = 0;
      /* debugLog.AddLog("[PPU] advancing 1 cycle (mode: %d, ly: %d, dot: %d, "
       */
      /*                 "next pixel: %d, pixels in fifo: %d, fetch pending:
       * %d)\n", */
      /*                 readPPUMode(state), ly, ppu->currentDot,
       * ppu->nextLCDPixel, */
      /*                 ppu->pixelsInFifo, ppu->fetchPendingCyclesRemaining);
       */
      // check which PPU mode we're in
      switch (readPPUMode(state)) {
      case 0: {
        // mode 0: HBlank (do nothing, wait until next scanline)
        if (ppu->currentDot == LINE_DOTS) {
          // move to the next scanline
          ppu->currentDot = 0;
          ly += 1;
          internal_writeMemory8(state, MEM_LY, ly);
          if (ly == VBLANK_LINE_START) {
            // if we've finished scanning the lcd, enter VBlank
            /* debugLog.AddLog("[PPU] Entering mode 1 VBLANK (ly=%d)\n", ly); */
            setPPUMode(state, 1);
            requestInterrupt(state, INTERRUPT_VBLANK);
          } else {
            // otherwise, enter mode 2
            setPPUMode(state, 2);
            doOAMScan(state);
          }
          /* debugLog.AddLog("[PPU] line %d - %d dots\n", ly - 1, dotsPerLine);
           */
          dotsPerMode = 0;
          dotsPerLine = 0;
        }
        cyclesThisStep++;
        break;
      }
      case 1: {
        // mode 1: VBlank (do nothing, wait until next frame)
        if (ppu->currentDot == LINE_DOTS) {
          // move to the next scanline and move to mode 2
          ppu->currentDot = 0;
          /* debugLog.AddLog("[PPU] line %d - %d dots\n", ly, dotsPerLine); */
          ly += 1;
          if (ly == MAX_LINES) {
            // if we've reached the last scanline, jump to the top and go to
            // mode 2
            ly = 0;
            internal_writeMemory8(state, MEM_LY, ly);
            setPPUMode(state, 2);
            debugLog.AddLog("[PPU] frame took %d dots (%d per scanline)\n",
                            dotsPerFrame, (dotsPerFrame / MAX_LINES));
            dotsPerMode = 0;
            dotsPerFrame = 0;
            /* debugLog.AddLog("[PPU] Entering mode 2 OAMSCAN (ly=%d)\n", ly);
             */
            doOAMScan(state);
          }
          dotsPerLine = 0;
          internal_writeMemory8(state, MEM_LY, ly);
        }
        cyclesThisStep++;
        break;
      }
      case 2: {
        // if we enter this mode with no OAM buffer, do the OAM scan here.
        if (!ppu->oamScanned) {
          doOAMScan(state);
          ppu->oamScanned = true;
        }
        // mode 2: OAM scan (queue up objects to be rendered on this scanline)
        if (ppu->currentDot == MODE2_DOTS) {
          setPPUMode(state, 3);
          ppu->oamScanned = false;
          dotsPerMode = 0;
          /* debugLog.AddLog("[PPU] Entering mode 3 LCDTRANSFER (ly=%d)\n", ly);
           */
          ppu->bgFetchPendingCyclesRemaining =
              doBGPixelFetch(state, ppu->bgFetcherBuffer);
          // the first fetch does not increment the internal X counter
          ppu->bufferFull = true;
        } else {
          cyclesThisStep++;
        }
        break;
      }
      case 3: {
        // mode 3: lcd pixel transfer (run the pixel FIFO/fetcher to push pixels
        // to the display)
        uint8_t wx;
        internal_readMemory8(state, MEM_WX, &wx);

        // check if we've reached the window, and initiate a window fetch
        if (ppu->nextLCDPixel == wx && windowEnable) {
          /* debugLog.AddLog( */
          /*     "[PPU] Reached window at %d; initiating window fetch\n", wx);
           */
          // flush the FIFO
          ppu->pixelsInBgFifo = 0;

          // start a window fetch
          ppu->isFetchingWindow = true;
          ppu->bgFetchPendingCyclesRemaining =
              doBGPixelFetch(state, ppu->bgFetcherBuffer);
          ppu->bufferFull = true;
        }

        // check if we need to render a sprite
        if (ppu->oamSpriteHead < 10 && ppu->spritesInBuffer > 0) {
          oam_sprite nextSprite = ppu->oamSpriteBuffer[ppu->oamSpriteHead];
          if (ppu->nextLCDPixel >= nextSprite.x) {
            /* debugLog.AddLog( */
            /*     "[PPU] Reached sprite at %d; initiating sprite fetch\n", */
            /*     ppu->nextLCDPixel); */
            // if we're at or after the sprite to be rendered, trigger a sprite
            // fetch
            // assumption: since we fetch in batches of 8 and sprites are
            // always 8 pixels wide, this check will guarantee we don't miss any
            // sprites
            ppu->oamFetchPendingCyclesRemaining =
                doSpritePixelFetch(state, ppu->oamFifo, nextSprite);
            ppu->oamSpriteHead++;
            ppu->pixelsInOamFifo = 8;

            // and flush the fetcher buffer
            ppu->bufferFull = false;
          }
        }

        if (ppu->pixelsInBgFifo > 0) {
          // as long as the fifo has pixels, attempt to push one
          if (ppu->pixelsInOamFifo > 0) {
            // if there are sprite pixels, mix them into the bg FIFO before
            // pushing the pixel to the LCD
            mixPixel(&ppu->bgFifo[0], &ppu->oamFifo[0]);

            // shift out the front oam pixel
            for (int i = 1; i < ppu->pixelsInOamFifo; i++) {
              ppu->oamFifo[i - 1] = ppu->oamFifo[i];
            }
            ppu->pixelsInOamFifo--;
          }

          // push the pixel to the screen, and shift out the front bg pixel
          transferPixel(state, graphicsBuffer, ppu->nextLCDPixel, ly,
                        ppu->bgFifo[0]);
          for (int i = 1; i < ppu->pixelsInBgFifo; i++) {
            ppu->bgFifo[i - 1] = ppu->bgFifo[i];
          }

          ppu->pixelsInBgFifo--;
          ppu->nextLCDPixel += 1;

          if (ppu->nextLCDPixel == EMULATOR_SCREEN_WIDTH) {
            // if there are pixels left in the fifo, one cycle is consumed for
            // each pixel remaining
            cyclesThisStep += ppu->pixelsInBgFifo;
            ppu->nextLCDPixel = 0;
            ppu->internalXCounter = 0;
            // once we've reached the end of the scanline, enter hblank
            setPPUMode(state, 0);
            dotsPerMode = 0;
            /* debugLog.AddLog("[PPU] Entering mode 0 HBLANK (ly=%d)\n", ly); */
          }
        } else {
          // if we have pending fetched pixels, push them into the fifo
          if (ppu->bufferFull) {
            if (ppu->bgFetchPendingCyclesRemaining == 0) {
              // do nothing, wait for the fetcher to tick down
              /* debugLog.AddLog("[PPU] Pushing pixels to FIFO\n"); */
              // push the buffer into the FIFO, empty the buffer, increment the
              // fetcher's internal x counter, and start a new fetch
              for (int i = 0; i < 8; i++) {
                ppu->bgFifo[ppu->pixelsInBgFifo++] = ppu->bgFetcherBuffer[i];
              }
              ppu->bufferFull = false;
              ppu->internalXCounter += 1;
            } else {
              ppu->bgFetchPendingCyclesRemaining--;
            }
          } else {
            ppu->bgFetchPendingCyclesRemaining =
                doBGPixelFetch(state, ppu->bgFetcherBuffer);

            ppu->bufferFull = true;
          }
        }

        cyclesThisStep++;
        break;
      }
      }
      ppu->currentDot += cyclesThisStep;
      dotsPerMode += cyclesThisStep;
      dotsPerLine += cyclesThisStep;
      dotsPerFrame += cyclesThisStep;
      cycles += cyclesThisStep;
    }
    ppu->cycleOverrun = cycles - cyclesToAdvance;
  }

  // Check STAT register and fire interrupts, if necessary
  bool nextStatResult = checkStatInterrupt(state);
  if (!ppu->lastStatResult && nextStatResult) {
    requestInterrupt(state, INTERRUPT_LCD);
  }

  ppu->lastStatResult = nextStatResult;
}

void resetPPUState(emulator_state *state) {
  dmg_ppu *ppu = &state->ppu;
  debugLog.AddLog("[PPU] resetting state\n");
  ppu->cycleOverrun = 0;
  ppu->currentDot = 0;
  ppu->nextLCDPixel = 0;
  ppu->internalXCounter = 0;
  ppu->windowLineCounter = 0;
  ppu->pixelsInBgFifo = 0;
  ppu->pixelsInOamFifo = 0;
  ppu->spritesInBuffer = 0;
  ppu->bufferFull = false;
  setPPUMode(state, 0);
}

uint8_t readPPUMode(emulator_state *state) {
  // mode 0: HBlank
  // mode 1: VBlank
  // mode 2: OAM scan
  // mode 3: lcd pixel transfer
  // OAM is inaccessible during modes 2 and 3
  // VRAM is inaccessible during mode 3
  uint8_t stat;
  internal_readMemory8(state, MEM_STAT, &stat);
  uint8_t mode = stat & 0x03;
  return mode;
}

void setPPUMode(emulator_state *state, uint8_t mode) {
  uint8_t stat;
  internal_readMemory8(state, MEM_STAT, &stat);
  // drop bits 0-1 and set them to the values in mode
  uint8_t next = (stat & 0xFC) | (mode & 0x03);
  internal_writeMemory8(state, MEM_STAT, next);
}
