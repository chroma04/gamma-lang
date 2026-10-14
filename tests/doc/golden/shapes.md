# Module `shapes`

A second file of the module. Files are read in name order, so this one comes first.

Shapes and what can be done with them.

A small module for the documentation generator's tests. It uses every kind of
public declaration:

    shapes :: import "./shapes"
    c := shapes.circle(2.0)

Markdown in comments is passed through: *emphasis*, **strong**, `code` and
[links](https://example.com).

## Structs

### `Options`

```gamma
Options :: struct {
    pub a: bool = false
    pub b: bool = false
}
```

Options in a table:

| name | meaning |
| --- | --- |
| `a` | first \| left |
| `b` | second |

1. one
2. two

### `Point`

```gamma
Point :: struct {
    pub x: f64 = 0.0
    pub y: f64 = 0.0
    // private fields omitted
}
```

A point in the plane.

Coordinates are in meters. The `<` and `&` characters stay text: a < b && c > d.

Fields:

- `x`: Distance to the right of the origin.
- `y`: Distance above the origin.

### `Pair`

```gamma
Pair :: struct(T: type) where Eq(T) {
    pub first, second: T
}
```

A pair of values of one type.

### `Counter`

```gamma
Counter :: struct {
    pub n: u32 = 0
}
```

Counts the shapes it hears about.

## Unions

### `Slot`

```gamma
#c_union
Slot :: union {
    Empty
    Int: i64
    Float: f64
}
```

Where a value is stored.

Variants:

- `Empty`: Nothing at all.
- `Int`: A whole number.

### `Shape`

```gamma
Shape :: union(u8) {
    Circle: f64
    Square: f64
}
```

A shape. Each variant holds its size.

- `Circle(radius)` is round;
- `Square(side)` has four equal sides.

## Type aliases

### `PointRef`

```gamma
PointRef :: ^Point
```

A pointer to a point.

### `Measure`

```gamma
Measure :: fn(s: ^const Shape) -> f64
```

A function that measures a shape.

### `Points`

```gamma
Points :: List(Point)
```

A list of points.

## Effects

### `Events`

```gamma
Events :: effect {
    created: fn(s: ^const Shape)
    abort: esc(reason: string) -> never
    name: ctl(s: ^const Shape) -> string
}
```

Reports what happens to shapes.

Operations:

- `created`: A shape was created.
- `abort`: Aborts the current computation.
- `name`: Asks for a name.

## Handlers

### `handler Events for ^Counter`

```gamma
handler Events for ^Counter
```

Counts creations.

## Constants

### `SIDES`

```gamma
SIDES: u8 : 3
```

The number of sides of a triangle.

### `PI_APPROX`

```gamma
PI_APPROX :: 355.0 / 113.0
```

A rational approximation of pi.

### `NO_DOC`

```gamma
NO_DOC: i32 : 7
```

### `TABLE`

```gamma
TABLE: [25]u32
```

A long table is not shown, only its type.

### `UNIT`

```gamma
// when target.os is .Windows
UNIT: string : "inch"
```

### `UNIT`

```gamma
// otherwise
UNIT: string : "meter"
```

Meters, except on Windows.

## Variables

### `last`

```gamma
last := Shape.Circle(0.0)
```

The last shape made, for debugging.

### `verbose`

```gamma
verbose: bool = false
```

Whether to print every shape.

## Functions

### `unit_square`

```gamma
unit_square :: fn() -> Shape
```

The unit square.

### `circle`

```gamma
circle :: fn(radius: f64) -> Shape
```

A circle of the given radius.

### `area`

```gamma
area :: fn(s: ^const Shape) -> f64
```

The area of `s`, in square meters.

```gamma
a := shapes.area(&shapes.circle(1.0))
```

### `copy_all`

```gamma
copy_all :: fn(
    T: type,
    from: []const T,
    n :: usize = 1,
    notify: ^fn(s: ^const T) \ {Events},
) -> AllocError![]T \ {Alloc, Events} where Plain(T)
```

Copies `n` shapes with the allocator in effect.

### `twice`

```gamma
#inline
twice :: fn(x: f64) -> f64
```

### `undocumented`

```gamma
undocumented :: fn()
```

## Foreign functions

### `native`

```gamma
native :: foreign "m" {
    sqrt :: fn(x: f64) -> f64 \ {}
}
```

Foreign helpers.

## Imports

### `alloc_mod`

```gamma
alloc_mod :: import "std/fixed_buffer"
```

Another name for the allocator module.
