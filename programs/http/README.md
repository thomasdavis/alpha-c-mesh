# @ajax/mesh-prog-http

An HTTP/1.0 GET server computed on the GPU: meshd only moves bytes; a warp parses, routes and renders
the whole response over the program table (`reads = ["*"]`): `/`, `/programs`, `/status`,
`/start/<name>`, `/stop/<name>`, `/view/<name>`, `/load/<name>`, `/doom`, `/train`, `/render`,
`/render.bmp`, `/render.ppm`; errors 400, 404, 405, 409, 503. Layout: @ajax/mesh-abi-view-http.

- **Ported from:** alpha-rust-mesh `programs/http/src/lib.rs`.
- **Tests:** on the simulator: every route's status line and a Content-Length equal to its body;
  /start and /stop's P_RUN_REQ; /load's request words, 503 until acked, 409 and 400; /view's declared
  fields; /train and /doom from those programs' views; /render.ppm and /render.bmp pixels in raster order.
  The number formatting was checked against the Rust's own on the same values.
