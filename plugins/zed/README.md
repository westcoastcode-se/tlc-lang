# TLC för Zed

Det här är Zed-pluginet under `plugins/`. Andra editorer får en egen mapp där.

Pluginet gör att Zed öppnar `.tlc` som TLC och kan köra programmet.

Det letar först efter `build/tlc` i projektroten. Finns den inte där används `tlc` från `PATH`.

## Installera

1. Öppna Extensions i Zed med Ctrl+Shift+X.
2. Klicka på **Install Dev Extension**.
3. Välj mappen `plugins/zed`.
4. Öppna en `.tlc`-fil.

Samma knapp finns i kommandopaletten (Ctrl+Shift+P) under namnet `zed: install dev extension`. Programmet `zed` i terminalen öppnar filer, så de orden ska inte skrivas där.

Zed bygger en liten Rust-del när tillägget installeras. Den startar `tlc lsp`. Rust från rustup behöver finnas. Servern är `build/tlc` i projektroten, annars `tlc` på `PATH`. Den ger diagnostik, komplettering, hovring, gå-till-definition och symboler.

Zed hämtar grammatiken från `grammars/tree-sitter-tlc`. Den mappen är ett eget git-arkiv, så låt `.git` där ligga kvar.

## Köra

I en `.tlc`-fil:

- `task: spawn` och välj **Run TLC program**. Det kör `tlc --run` på filen.
- **Check TLC program** typkontrollerar utan att köra.
- **Build TLC debug binary** skriver en körbar fil med `-g` till `build/debug/`.
- På raden `func main` finns en körknapp. Den använder samma körning.

Samma kommandon finns också i projektets `.zed/tasks.json`, så de syns i det här projektet innan pluginet är installerat. Välj då den `.tlc`-fil du vill köra.

## Debugga

Öppna `.tlc`-filen och kör `debugger: start`. Välj **Debug TLC program**. Zed bygger filen med `-g` och startar den i CodeLLDB. **Debug TLC program (GDB)** använder GDB i stället.

Klicka i marginalen för en brytpunkt. Sessionen stannar där, och du kan stega och läsa parametrar och lokala variabler. En filscopad `const` har ingen adress och syns inte.
