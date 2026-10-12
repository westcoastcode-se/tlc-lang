# TLC language server

Servern är `tlc lsp`. Den talar Language Server Protocol på standard in och ut. Den ingår i kompilatorn och är inte knuten till en editor.

Källan ligger i den här mappen. Binären `tlc` bygger in den.

## Start

```text
tlc lsp
```

Editorn skickar bufferten. Servern publicerar diagnostik och svarar på komplettering, hovring, gå-till-definition och dokumentsymboler. Binären är `build/tlc` i projektroten, eller `tlc` på `PATH`.

## Editorer

Zed startar samma kommando från `plugins/zed`. En annan editor gör samma sak: kör `tlc lsp` och tala protokollet på standardströmmarna.
