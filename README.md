# glob-cpp

A full-featured C++ glob matching library for Bash 4.3 glob behaviour.  

## API

```cpp
#include "Match.cpp"

// brace expansion
Glob js("**.{js,es6,mjs,cjs,pac}");
js.Matches("index.js");     // true
js.Matches("src/index.js"); // false
Glob js2("**/**.{js,es6,mjs,cjs,pac}");
js2.Matches("index.js");         // true
js2.Matches("src/index.js");     // true
js2.Matches("src/util/Util.js"); // true
// expansion set
Glob alpha("**/{a..c}"); // **/a, **/b, **/c
Glob numeric("**/{1..3}"); // **/1, **/2, **/3
// expansion set with incrementor
Glob alphaIncr("**/{a..e..2}"); // **/a, **/c, **/e
Glob numIncr("{0..12..4}"); // 0, 4, 8, 12
```