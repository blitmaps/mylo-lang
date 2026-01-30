![A Derpy Logo](docs/mylogo.png)
# Mylo

Mylo is an experimental language implemented in C, that uses a simple VM and has cool syntax. It has almost seamless C-interoperability.
Like a sausage dog, it is not serious, but it is cool.

#### 🦗 Mylo is alpha, so expect bugs.

Check out the [Language Reference](docs/mylo_reference.md).

## Hello Mylo
Here is `hello.mylo`:
```javascript
print("Hello Mylo")
```
We can run it like this:
```shell
> ./mylo hello.mylo

Hello Mylo
```

## Something a bit more complex
Here is fib.mylo:
```javascript
fn fib(n) {
  if (n < 2) {
    ret n
  }
  // Recursively call fib
  ret fib(n - 1) + fib(n - 2)
}
print("Calculating Fib(10)...")
var result = fib(10)
// With string interpolation
print(f"result : {result}")
```
Here's the ouput
```shell
> mylo fib.mylo
Calculating Fib(10)...
result : 55
```

## Running a Mylo program
```bash
> mylo
  Usage: mylo [--run|--build|--bind] <file> [--dump] [--trace]
```
#### --run
This option will run a mylo source file (like hello.mylo). It is the default.

#### --build
This will compile the mylo and inline C code, and produce out.c, for further compilation using a c-compiler.

#### --bind
This will produce a shared library binding for a Mylo file containing inline C. See the language reference for 
detailed information on creating a binding.

#### --dump
This will dump the VM assembly, as well as run the code.

#### --trace
This will show every VM transaction.

#### --debug
Implements the DAP protocol for debugging in VSCode or other editors.

## Building Mylo

### The simple way
```bash
 # Call the c compiler with source, and link math.
> cc src/*.c -o mylo -lm
```
### Using CMake (also builds tests)
```bash
> cmake -B build
> cd build
> make
> ./tests
```

## Debugging in VSCode
Mylo supports the DAP protocol. This can be used with any tool that supports DAP, using the `--debug` flag. There
is a VSCode plugin, in `editors/mylo-vscode` that configures Mylo automatically.

![VSCode Screenshot](docs/vscode.png)


## But everything cool is written in C, how do I use those libraries?
### Building a native binary with inline C?
Here is some Mylo code jumping into C and back...
```javascript
// 1. Explicit return: num
var result: num = C(val: int = 25) -> num {
    double res = sqrt(val);
    printf("Inside C: sqrt(%d) = %f\n", (int)val, res);
    return res;
}

print(f"Back in Mylo: Result is {result}")
```

For more information, read the FFI section of the reference.

We build it by producing a C source file with embedded VM code and wrapper code pre-generated.

```shell
# This produces an out.c file
> mylo --build example.mylo
```

We then just compile it, like any other C file, but we need to include
the Mylo VM source and headers.
```shell
# This produces mylo_app
>  gcc out.c src/vm.c src/mylolib.c -o mylo_app -Isrc -lm  
```

Our application can then be executed (without a runtime):
```shell
# Run
> ./mylo_app
  Inside C: sqrt(25) = 5.000000
  Back in Mylo: Result is 5
```

## Integration with CMake
Mylo applications can be built, C applications bound, and integrated into other applications
with CMake. There are [docs here.](cmake/README.md)


## Mylo REPL

Mylo can be launched in interpreter mode to evaluate commands from the CLI. It can also be used
to load native dlls and to import other mylo modules.

Launch the Mylo repl with `mylo --repl`. An example can be seen below running in Powershell:

```powershell
PS C:\Users\Brad\Documents\mylo-lang\build> .\mylo.exe --repl
Mylo REPL v0.1.5
Type 'exit' to quit.
> var x = range(20, 2, -2)
> print(x)
[20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0, -2]
> 
```