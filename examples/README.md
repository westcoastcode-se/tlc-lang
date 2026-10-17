# Examples

## Triangle

`examples/triangle` opens an SDL3 window and draws a colored triangle with OpenGL.

The bindings live in `lib/tlc/vendor/sdl3` and `lib/tlc/vendor/gl`. The program imports them as `vendor/sdl3` and `vendor/gl`. Constants such as `SdlInit.SDL_INIT_VIDEO` and `GL.TRIANGLES` are `extern(abi="c") type ... enum` values, so they match the C libraries. `vendor/gl` also defines the core entry points, such as `CreateShader`. `load` binds each one with `SDL_GL_GetProcAddress`. The example compiles its own shader. `tlc` finds that directory itself and links `libSDL3` and `libGL` from the `link` file in each package. Both libraries must be installed.

```sh
build/tlc --run examples/triangle/main.tlc
```

Close the window to exit. The context is an OpenGL 3.3 core profile. After the context is current, the example calls `load`, then `Viewport`, `CreateShader`, and the other functions from `vendor/gl`. It compiles the shader and draws a vertex buffer of positions and colors.

## Tetris

`examples/tetris` is a playable game in the same window. It calls the same `vendor/gl` functions and compiles its own shader. Blocks are colored quads. The board is a `[200]i32`. Piece cells are the file-scope constants `shape_x` and `shape_y`, each a `[28]i32`. A piece reads four cells with `shape_x[start:start + 4]`. Pieces rotate in the program, and `SDL_GetTicks` times the drop.

Left, right, and down move. Up rotates clockwise, Z rotates the other way, and space drops the piece. R starts again after the stack reaches the top. Escape or closing the window quits. Line clears and the score are printed in the terminal. A column of marks on the left counts cleared lines, and the next piece is shown on the right.

```sh
build/tlc --run examples/tetris/main.tlc
```
