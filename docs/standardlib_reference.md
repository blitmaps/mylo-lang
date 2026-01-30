<!-- TOC --><a name="mylo-standard-library-documentation"></a>
# Mylo Standard Library Documentation

The Mylo Standard Library provides essential functions for file I/O, math, and data manipulation. These functions are built-in and available directly in the interpreter.

<!-- TOC start (generated with https://github.com/derlin/bitdowntoc) -->

- [Mylo Standard Library Documentation](#mylo-standard-library-documentation)
  * [Math Functions](#math-functions)
    + [`sqrt(value: num) -> num`](#sqrtvalue-num-num)
  * [Utility Functions](#utility-functions)
    + [`len(collection: any) -> num`](#lencollection-any-num)
    + [`contains(haystack: any, needle: any) -> num`](#containshaystack-any-needle-any-num)
    + [`list(size: num) -> arr`](#listsize-num---arr)
    + [`add(array: arr, index: num, value: any) -> arr`](#addarray-arr-index-num-value-any---arr)
    + [`remove(collection: any, key: any) -> obj`](#removecollection-any-key-any---obj)
  * [Type Conversion](#type-conversion)
    + [`to_string(value: any) -> str`](#to_string)
    + [`to_num(value: any) -> num`](#to_num)
  * [File I/O (Text)](#file-io-text)
    + [`read_lines(path: str) -> arr`](#read_linespath-str-arr)
    + [`write_file(path: str, content: str, mode: str) -> num`](#write_filepath-str-content-str-mode-str-num)
  * [File I/O (Binary)](#file-io-binary)
    + [`read_bytes(path: str, stride: num) -> arr`](#read_bytespath-str-stride-num-arr)
    + [`write_bytes(path: str, data: arr) -> num`](#write_bytespath-str-data-arr-num)

<!-- TOC end -->

<!-- TOC --><a name="math-functions"></a>
## Math Functions

<!-- TOC --><a name="sqrtvalue-num-num"></a>
### `sqrt(value: num) -> num`

Calculates the square root of a number.

**Arguments:**
* `value`: The number to calculate the square root of.

**Returns:**
* The square root as a number.

**Example:**
```javascript
var result = sqrt(16)
print(result) // 4
```

<!-- TOC --><a name="utility-functions"></a>
## Utility Functions

<!-- TOC --><a name="lencollection-any-num"></a>
### `len(collection: any) -> num`

Returns the number of elements in an array or the number of characters in a string.

**Arguments:**
* `collection`: The array or string to check.

**Returns:**
* The length as a number.

**Example:**
```javascript
var list = ["A", "B", "C"]
print(len(list)) // 3

var s = "Hello"
print(len(s)) // 5
```

<!-- TOC --><a name="containshaystack-any-needle-any-num"></a>
### `contains(haystack: any, needle: any) -> num`

Checks if a value exists within a collection.

**Arguments:**
* `haystack`: The collection (Array or String) to search in.
* `needle`: The value to search for.
  * If `haystack` is an **Array**, `needle` can be any type.
  * If `haystack` is a **String**, `needle` must be a substring.

**Returns:**
* `1` if found, `0` if not found.

**Example:**
```javascript
// Array Check
var list = ["apple", "banana", "cherry"]
if (contains(list, "banana")) {
    print("Found banana!")
}

// String Check
if (contains("Teamwork", "work")) {
    print("Found substring!")
}
```

<a name="splitsourcestr-delimiterstr-arr"></a>
### `split(source: str, delimiter: str) -> arr`

Splits a string into an array of substrings based on a delimiter.

**Arguments:**
* `source`: The string to split.
* `delimiter`: The string pattern to split by. If empty `""`, splits into individual characters.

**Returns:**
* An array of strings.

**Example:**
```javascript
var s = "a,b,c"
var parts = split(s, ",")
print(parts[0]) // "a"
print(parts[1]) // "b"

var word = "cat"
for (x in split(word, "")) {
	print(x)
}
// ["c", "a", "t"]
```

<a name="wherecollectionany-itemany-num"></a>
### `where(collection: any, item: any) -> num`

Finds the index of the first occurrence of an item within a collection.

**Arguments:**
* `collection`: The string or array to search.
* `item`: The value to search for.

**Returns:**
* The index (0-based) of the found item, or `-1` if not found.

**Example:**
```javascript
// Arrays
var list = ["cat", "dog"]
print(where(list, "dog")) // 1
print(where(list, "fish")) // -1

// Strings
var text = "ohblast"
print(where(text, "blast")) // 2
```

<a name="listsize-num-arr"></a>
### `list(size: num) -> arr`

Creates a new array of a specific size, initialized with zeros.

**Arguments:**
* `size`: The number of elements to allocate.

**Returns:**
* A new array containing `size` zeros.

**Example:**
```javascript
var buffer = list(10)
print(len(buffer)) // 10
print(buffer[0])   // 0
```

<a name="addarray-arr-index-num-value-any-arr"></a>
### `add(array: arr, index: num, value: any) -> arr`

Inserts a value into an array at a specific index, shifting subsequent elements to the right.

**Important:** Arrays in Mylo are fixed-size in memory. This function creates and returns a **new** array. You must assign the result back to your variable to see the change.

**Arguments:**
* `array`: The source array.
* `index`: The position to insert the value (0 to len).
* `value`: The value to insert.

**Returns:**
* A **new** array with the element added.

**Example:**
```javascript
var weights = [10, 20]
// Insert 15 at index 1
weights = add(weights, 1, 15) 
print(weights) // [10, 15, 20]
```

<a name="removecollection-any-key-any-obj"></a>
### `remove(collection: any, key: any) -> obj`

Removes an element from a collection **in-place**.

**Arguments:**
* `collection`: The array or map to modify.
* `key`: The identifier for the item to remove.
  * **Array**: The numeric **index** of the item. Elements after this index are shifted left.
  * **Map**: The **key** (string) of the entry to remove.

**Returns:**
* Returns the modified collection to support chaining (e.g., `remove(arr, 0)`).

**Example:**
```javascript
// Array Removal
var items = ["A", "B", "C"]
remove(items, 1) // Remove index 1 ("B")
print(items) // ["A", "C"]

// Map Removal
var data = {"name"="Mylo", "ver"=1}
remove(data, "ver")
print(data) // {"name"="Mylo"}
```

<!-- TOC --><a name="type-conversion"></a>
## Type Conversion
<!-- TOC --><a name="to_string"></a>
### `to_string(value: any) -> str`

Converts a value to its string representation.

**Arguments:**
* `value`: The value to convert.

**Returns:**
* The string representation of the value.

**Example:**
```javascript
var n = 123
var s = to_string(n) 
print(s + "4") // "1234" (concatenation, not math)
```
<!-- TOC --><a name="to_num"></a>
### `to_num(value: any) -> num`

Converts a value (usually a string) to a number.

**Arguments:**
* `value`: The string to convert.

**Returns:**
* The numeric value. Returns `0` if the conversion fails.

**Example:**
```javascript
var s = "45.5"
var n = to_num(s)
print(n + 10) // 55.5
```

<!-- TOC --><a name="file-io-text"></a>
## File I/O (Text)

<!-- TOC --><a name="read_linespath-str-arr"></a>
### `read_lines(path: str) -> arr`

Reads a text file and returns its content as an array of strings, where each element represents one line of the file.

**Arguments:**
* `path`: The path to the file.

**Returns:**
* An array of strings. Returns an empty array if the file cannot be read.

**Example:**
```javascript
var lines = read_lines("data.txt")
for (line in lines) {
    print(line)
}
```

<!-- TOC --><a name="write_filepath-str-content-str-mode-str-num"></a>
### `write_file(path: str, content: str, mode: str) -> num`

Writes a string to a file. Supports overwriting or appending.

**Arguments:**
* `path`: The path to the file.
* `content`: The string data to write.
* `mode`: The file mode.
  * `"w"`: Write (Overwrite). Creates the file if it doesn't exist, or truncates it if it does.
  * `"a"`: Append. Writes data to the end of the file.

**Returns:**
* `1` on success, `0` on failure.

**Example:**
```javascript
// Create/Overwrite file
write_file("log.txt", "Log Started\n", "w")

// Append to file
write_file("log.txt", "New Entry\n", "a")
```

<!-- TOC --><a name="file-io-binary"></a>
## File I/O (Binary)

<!-- TOC --><a name="read_bytespath-str-stride-num-arr"></a>
### `read_bytes(path: str, stride: num) -> arr`

Reads a file in binary mode and returns the data as an array of numbers.

**Arguments:**
* `path`: The path to the file.
* `stride`: The size of each element to read.
  * `1`: Read as individual bytes (0-255).
  * `4`: Read as 32-bit Integers (Little Endian).

**Returns:**
* An array of numbers representing the file content.

**Example:**
```javascript
// Read as raw bytes
var bytes = read_bytes("image.png", 1)
print(bytes[0]) // First byte (e.g., 137 for PNG)

// Read as 32-bit integers
var ints = read_bytes("data.bin", 4)
print(ints[0])
```

<!-- TOC --><a name="write_bytespath-str-data-arr-num"></a>
### `write_bytes(path: str, data: arr) -> num`

Writes an array of numbers to a file as raw bytes.

**Arguments:**
* `path`: The path to the file.
* `data`: An array of numbers. Each number is cast to a byte (0-255) before writing.

**Returns:**
* `1` on success, `0` on failure.

**Example:**
```javascript
// Write ASCII bytes for "ABC\n"
var data = [65, 66, 67, 10]
write_bytes("output.bin", data)
```