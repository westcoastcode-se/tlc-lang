# Examples

## Triangle

`examples/triangle` opens an SDL3 window and draws a colored triangle with OpenGL.

The bindings live in `lib/tlc/vendor/sdl3` and `lib/tlc/vendor/gl`. The program imports them as `vendor/sdl3` and `vendor/gl`. `tlc` finds that directory itself and links `libSDL3` and `libGL` from the `link` file in each package. Both libraries must be installed.

```sh
build/tlc --run examples/triangle/main.tlc
```

Close the window to exit. The context is an OpenGL 2.1 compatibility profile, so the triangle is drawn with `glBegin`. Core-profile entry points are loaded at run time, and this dialect cannot cast a function pointer yet.
