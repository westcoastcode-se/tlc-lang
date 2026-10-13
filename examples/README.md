# Examples

## Triangle

`examples/triangle` opens an SDL3 window and draws a colored triangle with OpenGL.

The bindings live in `lib/tlc/vendor/sdl3` and `lib/tlc/vendor/gl`. The program imports them as `vendor/sdl3` and `vendor/gl`. Constants such as `SdlInit.SDL_INIT_VIDEO` and `Gl.GL_TRIANGLES` are `extern(abi="c") type ... enum` values, so they match the C libraries. `tlc` finds that directory itself and links `libSDL3` and `libGL` from the `link` file in each package. Both libraries must be installed.

```sh
build/tlc --run examples/triangle/main.tlc
```

Close the window to exit. The context is an OpenGL 2.1 compatibility profile, so the triangle is drawn with `glBegin`. Core-profile entry points are loaded at run time, and this dialect cannot cast a function pointer yet.

## Tetris

`examples/tetris` is a playable game in the same window. Blocks are colored quads made from `glBegin` triangles. The board is a `[200]i32`. Pieces rotate in the program, and `SDL_GetTicks` times the drop.

Left, right, and down move. Up rotates clockwise, Z rotates the other way, and space drops the piece. R starts again after the stack reaches the top. Escape or closing the window quits. Line clears and the score are printed in the terminal. A column of marks on the left counts cleared lines, and the next piece is shown on the right.

```sh
build/tlc --run examples/tetris/main.tlc
```
