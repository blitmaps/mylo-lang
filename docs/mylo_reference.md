<!-- TOC --><a name="mylo-reference"></a>
# Mylo Reference

<!-- TOC --><a name="language-basics"></a>
## Language Basics
Mylo uses familiar programming concepts: variables, loops, conditional statements, functions, modules etc.
It is fast to write, fast to run, and can run anywhere.

<!-- TOC start -->

### Contents
- [Mylo Reference](#mylo-reference)
    * [Language Basics](#language-basics)
    * [Technical Intro (for nerds)](#technical-intro-for-nerds)
    * [Variables](#variables)
        + [Numbers and Strings](#numbers-and-strings)
    * [Types](#types)
    * [Lists/Arrays](#listsarrays)
        + [Arrays of Structs](#arrays-of-structs)
        + [Adding to or Concatenating Arrays](#adding-to-or-concatenating-arrays)
        + [Sub-Arrays or Array-Slicing](#sub-arrays-or-array-slicing)
    * [Maps](#maps)
    * [Control flow](#control-flow)
        + [Conditionals (If Statements)](#conditionals-if-statements)
        + [For loops](#for-loops)
            - [Range Based](#range-based)
            - [Checked](#checked)
            - [Iterated](#iterated)
            - [Forever](#forever)
            - [Break & Continue](#break-and-continue)
    * [Functions](#functions)
        - [Passing own types](#passing-own-types)
    * [Scope & Modules](#scope-modules)
    * [Import & Module Path](#import)
    * [C Interoperability Foreign Function Interface (FFI)](#c-interoperability-foreign-function-interface-ffi)
        + [Source example.mylo](#source-examplemylo)
        + [Passing variables to C](#passing-variables-to-c)
            - [Getting values back](#getting-values-back)
            - [Sending values to C](#sending-values-to-c)

<!-- TOC end -->


<!-- TOC --><a name="technical-intro-for-nerds"></a>
## Technical Intro (for nerds)
Mylo uses a high-level syntax like Python or Javascript, but is *strongly typed*. Code
is compiled to a virtual machine (VM), and it is executed on any platform with Mylo installed. Mylo also
supports compiling to a binary, with embedded VM code, as well as interfacing with native libraries.

<!-- TOC --><a name="variables"></a>
## Variables
<!-- TOC --><a name="numbers-and-strings"></a>
### Numbers and Strings
Mylo is strongly typed, but types are inferred for numbers, strings, bools, enums, maps and lists. Here is how to define variables.
```javascript
var a_number = 42
var a_string = "Hi"
```
Mylo handles variable overrides for you, so this is ok:
```javascript
var my_var = 40
// This is a comment by the way. Below we override my_var with a string.
my_var = "Oh no!"
```

<!-- TOC --><a name="types"></a>
## Types
### Structs
You can make your own types using the `struct` keyword.
The type is specified after var using the `:` operator
```javascript
struct Color {
    var r
    var g
    var b
}

// Very red
var my_colour: Colour = { r=255, g=0, b=0 }
```
### Enums
You can also define your own enumerated variations on types like this:

```javascript
enum Transport {
    car,
    bus,
    bike
}
var myvar = Transport::car
if (myvar == Transport::car) {
    print("He's in a car!")
}
```

### Bools
Truth statements evaluate to 0 for false and 1 for true. These are also codified as `true` and `false`

```javascript
var a = "foo"
if (true == false) { // Never true
    // never executes
    a = "bar"
}
// "foo"
print(a)
```

<!-- TOC --><a name="listsarrays"></a>
## Lists/Arrays

Arrays for string and numbers can be defined just like regular variables,
using the `[]` syntax.
```javascript
var my_list = ["a", "b", "c"]
var my_list = [1, 2, 3]
```

<!-- TOC --><a name="arrays-of-structs"></a>
### Arrays of Structs
Arrays of a given struct are defined as usual with type information, and specified with `[]` operators.

```javascript
struct Color {
    var rgba
}
var my_list: Color = [{rgba=1000}, {rgba=2000}]
```

<!-- TOC --><a name="adding-to-or-concatenating-arrays"></a>
### Adding to or Concatenating Arrays
Elements can be added to an array with the `+` operator, with the
elements to be added in braces `[]`.

```javascript
struct Color {
    var rgba
}
// Empty list of Colors
var my_list: Color = []
// Add an element
my_list = mylist + [{rgba: 500}]

// Print the first
print(my_list[0])
```

<!-- TOC --><a name="sub-arrays-or-array-slicing"></a>
### Sub-Arrays or Array-Slicing
Elements can be sliced using the `:` operator in the index. Inclusive Slicing (like Ruby ranges or standard math [start, end]) is used so:

- `[0:1]` returning indices 0 and 1 (first and next).
- `[1:3]` returning indices 1, 2, and 3 (second, third and fourth element).
- `[-1]`  returning the last element.
```javascript
struct Color {
    var rgba
}

// Start empty
var my_list: Color = []
// Put some values in
my_list = my_list + [{rgba=500}, {rgba=400}, {rgba=300}, {rgba=700}, {rgba=900}]

// Slice to get [{400}, {300}, {700}]
my_list = my_list[1:3]

// Print them out
for (x: Color in my_list) {
    print(x.rgba)
}
```

<!-- TOC --><a name="maps"></a>

## Maps (Dictionaries)

Maps are key-value collections where keys are always strings and values can be any type. They are dynamic and will grow as new keys are added.
You can create a map using the map literal syntax `{}`. Keys must be strings, and assignments use the `=` operator.

```javascript
// Empty Map
var empty = {}

// Map with initial values
var user = {
    "name" = "Alice",
    "age" = 30,
    "role" = "Admin"
}
```

### Accessing Values

Values are retrieved using the bracket syntax `["key"]`.

**Note:** If a key does not exist, accessing it returns an empty string `""` (or default value depending on implementation context).

```javascript
var name = user["name"]
print(name) // "Alice"
```

### Modifying and Inserting

Maps are mutable. You can update existing keys or add new ones using the bracket assignment syntax.

```javascript
// Update existing
user["age"] = 31

// Insert new key
user["status"] = "Active"
```

<!-- TOC --><a name="control-flow"></a>
## Control flow

<!-- TOC --><a name="conditionals-if-statements"></a>
### Conditionals (If Statements)
We can control the flow of our program like this.
```javascript
var age = 5
// This will print "Hello Little One"
if (age < 10) {
    print("Hello Little One")
} else {
    print("Hello ... not Little One??")
}
```

Mylo supports binary comparisons the same as C:
less than`<`, less than or equal to`<=`, greater than`>`, greater than or equal to`>=`, not equal `!=` and  equal to`==`.

<!-- TOC --><a name="for-loops"></a>
### For loops
For loops can be range based, checked or iterated. See here:
<!-- TOC --><a name="range-based"></a>
#### Range Based
```javascript
// This is a loop to count from 0 to 4
for (var x in 0...5) {
    print(x)
}
// You can also go the other way 5...0
```
output:
```shell
0
1
2
3
4
```

<!-- TOC --><a name="checked"></a>
#### Checked
Checked loops check a value each iteration, they could loop
forever if the checked value is never updated.
```javascript
var index = 0
for (index <= 100) {
    print(index)
    // If you comment this out, it will loop forever
    index = index + 1
}
```

<!-- TOC --><a name="iterated"></a>
#### Iterated
Lists can be iterated over, using the `in` keyword.
```javascript
var my_pets = ["dog", "cat", "fish"]
// notice we didn't need 'var x'
for (x in my_pets) {
    // This will print dog, then cat, then fish
    // each on a new line.
    print(x)
}
```
<!-- TOC --><a name="forever"></a>
#### Forever
You can just keep executing a block forever, or until you break out.
```javascript
var i = 0
forever {
    print(i)
    i = i + 1
    
    if (i > 10) {
        break
    }
}
```
<!-- TOC --><a name="break-continue"></a>
#### Break and Continue
You can exit a loop early or jump back and begin the next iteration with `break` and `continue`

```javascript
for (var x in 0...5) {
    if (x == 1) {
        break
    }
    print(x)
}

for (var x in 0...100) {
    if (x < 50) {
        continue
    }
    print(x)
}
```

<!-- TOC --><a name="functions"></a>
## Functions

Subroutines/Functions can be defined to allow variables to be passed
and *values* returned.

```javascript
// This function takes an input 'n' and returns 
// n + 1
fn add_one_to_my_number(n)
{
    ret n + 1
}

var my_num = 99

print(add_one_to_my_number(my_num))
// ^^ prints 100 
print(my_num)
// ^^ prints 99? Why?
// because my_num was copied to add_one_to_my_number
// and the output was passed to print

// Now what happens?
my_num = add_one_to_my_number(my_num)
print(my_num)
```

<!-- TOC --><a name="passing-own-types"></a>
#### Passing own types

Functions can reference any type, but use *Strict Types*. This example show how to use a custom type.
The last line shows how Runtime Type Checking is used to make sure functions are compatible.
```javascript
struct foo {
    var name
}

// Will will pass a list of foo, this must be specified
fn list_names(n : foo[])
{
    // x needs to be defined as a foo.
    for (x: foo in n) {
        print(x.name)
    }
    // done.
    ret
}

// define some foos.
var myvar0: foo = [{name="Harry"}, {name="Barry"}, {name="John"}]

// This prints 'Harry', 'Barry', 'John'
list_names(myvar0)
// This would fail, as 99 is not an obj foo[] (list of foo).
// Runtime Error: Type Mismatch. Expected struct type 0, got 1
// list_names([99])
```

<!-- TOC --><a name="scope-modules"></a>
## Scope & Modules

Variables inside functions have their own scope unless they are passed in.

```javascript
var x = 5

fn foo() {
    var x = 10
    ret x
}

// Prints 5
print(x)
// Prints 10
print(foo())
```

Functions and variables can be gathered into Modules, and accessed with the `::` operator.

```javascript

fn a_big_number() {
    ret 65535
}

mod MyFirstMod {
    // Same name as above, but in a module.
    fn a_big_number() {
        ret 99999
    }
}

// Prints 99999
print(MyFirstMod::a_big_number())

// Prints 65556
print(a_big_number())
```

<!-- TOC --><a name="import"></a>
## Import - Using other Mylo files
Other files, containing modules can be imported into the program using `import`, and search path can be added with
`module_path()`.

Here is an example, see `test_import.mylo`

```javascript
fn my_test() {
    print("3")
}
```

Now in our main.mylo:
```javascript
    // The mylo file is in tests, and we usually would run from either the 
    // top level or build directory, so I've added some relative module paths
    // to search
    module_path("../tests/")
    module_path("tests/")
    module_path("../../tests/")
    // my_test is imported here
    import "test_import.mylo"
    
    // This prints 3
    my_test()
```

<!-- TOC --><a name="c-interoperability-foreign-function-interface-ffi"></a>
## C Interoperability Foreign Function Interface (FFI)

Most software, especially that running at high speed, interfacing with hardware, is written either in C or with a C API.

To make Mylo useful, C code can be compiled within a Mylo source file.
This is achieved by compiling the VM assembly and embedding the VM into a C-application, and generating interfaces
with C variables and functions.

<!-- TOC --><a name="source-examplemylo"></a>
### Source example.mylo
```javascript
struct foo {
    var x
}


// Uses "c_foo" automatically, Mylo will make
// this struct available at compile time.
var oh_my: foo = C() -> foo {
    c_foo val;
    val.x = 6;
    return val;
}

print(f"foo.x : {oh_my.x}")
```

We build it by producing a C source file with embedded VM code and wrapper code pre-generated.

```shell
# This produces an out.c file
> mylo --build example.mylo
```

We then just compile it, like any other C file, but we need to include
the Mylo VM source and headers.
```shell
# This produces mylo_app
> gcc out.c ../src/vm.c -o mylo_app -I../src -lm
```

Our application can then be executed (without a runtime):
```shell
# Run
> ./mylo_app
  foo.x : 6
```

<!-- TOC --><a name="passing-variables-to-c"></a>
### Passing variables to C

It's important to be able to pass information to C, and crucially, to get it back again!
This includes arbitrary structures from C!

<!-- TOC --><a name="getting-values-back"></a>
#### Getting values back
Mylo's numbers are doubles as default, so we can cast chars or other types and pass them back from C:
```javascript
struct Color {
    var r
    var g
    var b
}

var pixel = C() -> Color {
    struct color_rgb { char r; char g; char b; };
    // Simulated native data: 255 (0xFF), 0 (0x00), 128 (0x80)
    struct color_rgb raw = { 255, 0, 128 };

    c_Color out;
    // FIX: Cast to (unsigned char) first to prevent negative numbers
    out.r = (double)(unsigned char)raw.r;
    out.g = (double)(unsigned char)raw.g;
    out.b = (double)(unsigned char)raw.b;

    return out;
}

print(pixel.r) // Now prints 255
print(pixel.b) // Now prints 128
```
<!-- TOC --><a name="sending-values-to-c"></a>
#### Sending values to C
Here we send an int, and a string to c, and print them using
printf.
````javascript
var a = 100
var b = "200"
var result: num = C(val: int = a, val2 : str = b) {
    printf("Inside C: %d, %s\n", (int)val, val2);
}
````

#### Getting Type Handles !

Libraries have custom types in C. Mylo has memory handlers which
allow a 'Handle' to those objects to be passed around in Mylo. In this example
Raylib's Image object is utilised.

```javascript
import C "raylib.h"

// 1. Return an ID (num)
var img_id: num = C() -> num {
    Image raw = LoadImage("cat.png");
    // "Image" is hashed automatically
    // and is used to remember the type.
    return MYLO_STORE(raw, "Image");
}

// Back in Mylo ...
// img_id is just a handle (num), and is only meaningful to C.
print(img_id)

// img_id is now passed back to C
C(id: num = img_id) {
    // 2. Retrieve it safely
    // Checks if ID exists AND if the stored hash matches hash("Image")
    Image* img = MYLO_RETRIEVE(id, Image, "Image");
    
    // Null if not found
    if (img) {
        // Here we can use the object again
        printf("Image size: %d x %d\n", img->width, img->height);
    }
}
```

#### Void blocks

Running C code can return no values
```javascript
// This works as a statement
C() {
    printf("I am a void C block!\n");
}

// Arguments still work
var x = 10
C(v: num = x) {
    printf("Value is: %f\n", v);
}

// This will now throw an error, you can't have a function 
// called C.
fn C() {
    print("Cannot do this")
}
```

### Including C Headers

To include C header code (to define functions to call in native blocks) you use 
the 'C' identifier in import statements
```javascript
import C "cool_func.h"

C() {
    cool_func();
}
```

### Standard Library
The standard functions are still rather limited, but can do file manipulation, array insights and some maths.
This is actively being developed to add more power to the VM.

Check out the [Standard Library Reference](standardlib_reference.md).
