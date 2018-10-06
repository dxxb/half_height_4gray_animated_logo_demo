/*
  Copyright 2017 Delio Brignoli - brignoli.delio@gmail.com

  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
  = Arduboy ssd1306 GDDRAM half-height double buffering demo

  == Theory of operation

  The number of displayed lines refreshed is reduced by setting
  ssd1306's COM mux ratio, in the example code below the mux ratio
  is set to 32 lines (half of the maximum). Each height frame is
  uploaded to the GDDRAM region *not* being rendered, then
  the set display line offset command is used to display the newly
  uploaded data.

  == Advantages and disadvantages

  The set display line offset command takes effect between frames
  so there is no tearing. However, the display is refreshed at
  higher frequency (twice as fast in this example because the number
  of lines is halved). When displaying grayscale, and in order to minimize
  flickering, the contents of GDDRAM have to be updated as fast as the
  display's frame rate. The exact frame rate is unknown, changes
  with time (part tolerances, temperature, battery voltage) and there
  is no mechanism available in "Arduboy V1" production hardware to
  control ssd1306's fosc or pace screen updates using some feedback
  from ssd1306 [1]. This in practice means that while there is no tearing,
  flickering is unavoidable to some degree but this example may still
  be useful for demos using grayscale or cutscenes.

  If anyone uses this technique it would be nice to be credited for it ;-)
  unless this turns out to be something everyone knew already and I was
  just late to the party!

  == Full screen grayscale

  I have investigated using this technique to achieve full screen
  grayscale by alternating ssd1306 refresh between the top and
  bottom half of the screen and my conclusion is that it is not viable.

  The technique involves reducing the mux ratio to half-height as above
  but both the start line offset and the screen display offset are changed
  during display blanking so they are applied at the same time
  between frames. Both commands involved take affect during blanking, but
  it appears the set display line offset command takes effect on current frame+2
  (i.e. during the second blanking period after the command is sent) so this
  has to be taken into account.

  While in the half screen case only one command is sent, in the full screen
  case two commands are sent back to back and it is possible for the second
  to be received too late to take effect in the next frame depending on the
  exact timing the command is sent in relation to the blanking period. [2]
  
  Additionally even if the non-atomicity of the two commands execution is ignored
  the implementation needs to make sure it is sending exactly one command sequence
  per frame which requires control of ssd1306's fosc or a feedback mechanism to
  notify the MCU of the blanking period neither of which is possible on "Arduboy V1"
  production hardware. So the same issue that makes tearing unavoidable
  when rendering the full screen at once exists in this case, only the effect
  is visually worse than tearing because when the glitch occurs the bottom half
  of the screen is rendered in the top half for a single frame before being corrected.
  The overall effect of the glitch is *at best* a ghosting image of one half of the screen
  appearing in the other half once in a while. Which is probably undesirable for
  most applications unless the glitchy behavior serves the theme of the application/demo
  itself.

  [1] The relevant pins are not available on the connector.
  [2] Interestingly the fact that the set display line offset command
      takes effect one frame later than expected would help working around
      the non-atomicity of two sequential commands in the case when the internal
      registers they affect are latched between the reception of the two commands.
      So this 'unexpected' (and undocumented) behavior of the set display
      line offset command may indeed be intentional.
*/

#include <Arduboy2.h>
#include "ab_logo.c"

Arduboy2 arduboy;
static unsigned long next_frame = 0;
unsigned long frame_period;

#define DEFAULT_FRAME_PERIOD_MS (7572)

static const uint8_t colors[4][3] = {
    {WHITE, WHITE, WHITE},
    {WHITE, BLACK, WHITE},
    {BLACK, WHITE, BLACK},
    {BLACK, BLACK, BLACK}
  };

static inline void sendcmds(const uint8_t *cmds, uint8_t len)
{
  arduboy.LCDCommandMode();
  while (!(SPSR & _BV(SPIF))) { }
  while (len--) {
    SPDR = *(cmds++);
    while (!(SPSR & _BV(SPIF))) { }
  }
  arduboy.LCDDataMode();
}

void ssd1306_select_gddram_half(const uint8_t idx)
{
  /*
   The 0x40 command (GDDRAM display start line offset)
   seems to behave in an unxpected manner in that the new
   line selection does not come into effect immediately
   or at the beginning of the next frame but at the
   beginning of current_frame+2. When rendering at close
   to the display's refresh rate this has to be taken into
   account.
  */
  const uint8_t cmds[] = {0x40+(idx % 2 ? 0 : 32)};
  sendcmds(cmds, sizeof(cmds));
}

void setup() {
  arduboy.boot();
  frame_period = DEFAULT_FRAME_PERIOD_MS;
  /*
   Set ssd1306 COM mux ratio to 32 lines and 
   display offset to 16 so the active section
   of the display is centered vertically.
  */
  const uint8_t cmds[] = {0xA8, 31, 0xD3, 16};
  sendcmds(cmds, sizeof(cmds));
  /* display top half of the controller's buffer */
  ssd1306_select_gddram_half(0);
}

static inline void test_pattern_rect(const uint8_t x, const uint8_t y,
                                     const uint8_t w, const uint8_t h,
                                     const uint8_t colors[4][3],
                                     const uint8_t c_ofs, const uint8_t i_ph)
{
  for (uint8_t i = 0; i < 3; i++) {
    arduboy.fillRect(x+i*w/3, y, w/3, h, colors[(c_ofs+i)%3][i_ph]);
  }
}

static inline render_logo(const uint8_t y_ofs)
{
  arduboy.drawBitmap(20, y_ofs, arduboy_logo, 88, 16);
}

static inline render_moving_bar(const uint16_t frame_idx, const uint8_t y_ofs)
{
  const uint8_t i_ph = frame_idx % 3;
  test_pattern_rect(16, y_ofs, 96, 2, colors, 0, i_ph);
  test_pattern_rect(16, y_ofs+2, 96, 2, colors, 1, i_ph);
  test_pattern_rect(16, y_ofs+4, 96, 2, colors, 2, i_ph);  
}

void render(const uint16_t frame_idx)
{
  /*
   White foreground ARDUBOY logo on black backgroud with one gray
   bar bouncing up and down
  */
  const uint8_t top_line = frame_idx % 2 ? 0 : 32;
  const uint8_t rt = (frame_idx/9)%((32-6)*2);
  const bool front = rt > (32-6);
  const uint8_t rect_rel_top = front ? (32-6)*2 - rt : rt;
  const uint8_t i_ph = frame_idx % 3;

  if (front) {
    test_pattern_rect(0, 0, 16, 3, colors, 2, i_ph);
    test_pattern_rect(0, 3, 16, 3, colors, 0, i_ph);
    render_moving_bar(frame_idx, top_line+rect_rel_top);
    render_logo(top_line+8);
  } else {
    test_pattern_rect(0, 0, 16, 3, colors, 2, i_ph);
    test_pattern_rect(0, 3, 16, 3, colors, 0, i_ph);
    render_logo(top_line+8);
    render_moving_bar(frame_idx, top_line+rect_rel_top);
  }

  /*
   Upload the fresh half of the frame buffer and
   wipe it to get it ready for the next round
  */
  const uint16_t half_buf_sz = 128*64/8/2;
  uint16_t ofs = 0;
  uint8_t *ptr = arduboy.sBuffer+(frame_idx % 2 ? 0 : half_buf_sz);
  SPDR = ptr[ofs];
  ptr[ofs++] = 0;
  while (ofs < half_buf_sz) {
    const uint8_t px = ptr[ofs];
    ptr[ofs++] = 0;
    /* wait for the previous word to shift out */
    while (!(SPSR & _BV(SPIF))) {}
    SPDR = px;
  }
  /* wait for the last word to shift out */
  while (!(SPSR & _BV(SPIF))) {}
}

void loop() {
  const unsigned long now = micros();
  const unsigned long rem = next_frame - now;
  if (rem < frame_period) {
    if (rem > 1500) {
      set_sleep_mode(SLEEP_MODE_IDLE);
      sleep_mode();
    }
    return;
  }
  next_frame = now + frame_period;
  arduboy.frameCount++;

  /*
   Alternate between drawing in the top and
   bottom half of the frame buffer
   each frame and transfer the half
   of the framebuffer we just prepared to
   the half of the display controller's
   GDDRAM that is *not* being displayed.
  */

  /* render the current half frame and upload it to the display */
  render(arduboy.frameCount);
  /* switch controller to display the new buffer we just uploaded */
  ssd1306_select_gddram_half(arduboy.frameCount);
}

