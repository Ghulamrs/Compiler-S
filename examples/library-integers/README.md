# Integers across the boundary

```
cd ../../../Compiler-C/examples/shalimar-library && ./build.sh
cd -                                             && ./build.sh
```

```
gcd of 84 and 36   12
sum                36
how many over 5    4
diagonal of m      15
after sorting      1 2 3 6 7 8 9
```

The companion to `using-a-library`, which is all reals. **Nothing here is a
real** — the point being that the boundary is not a floating-point one.

| Shalimar | C | read with |
| --- | --- | --- |
| `a: int` | `int` | — 32 bits on all three targets |
| `a[]: int` | `const ShmArray *` | `shm_get_int` |
| `m[][]: int` | `const ShmArray *` | `shm_get_ref` then `shm_get_int` |

`shm_array_dim` gives the length whatever the element type; only the element
accessor changes between an `int[]` and a `real[]`.

`tally_sort` writes through the handle. Shalimar passes arrays by reference, so
the sort the C performed is what the last line prints.

The C is `tally.c` in the library directory.
