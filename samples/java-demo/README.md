# java-demo

A small order-pricing library in plain Java, used as a sample project for the
workbench editor. It has a value type for money, a cart that applies an injected
tax policy, a receipt renderer, and a dependency-free test runner.

Requires only a JDK (17 or newer; the build compiles with `--release 17`).
No Maven, Gradle, or JUnit.

## Build and test

From anywhere:

```sh
sh samples/java-demo/build.sh
```

The script resolves paths from its own location, so the current directory does not
matter. It:

1. Compiles `src/` into `out/classes`.
2. Compiles `test/` into `out/test-classes`.
3. Runs `demo.TestRunner`, which prints `PASS`/`FAIL` per test and exits 1 if any test fails.

To run the sample receipt:

```sh
java -cp samples/java-demo/out/classes demo.Main
```

## Layout

```
samples/java-demo/
  build.sh                  build and test entry point (POSIX sh)
  src/demo/
    Money.java              value type in whole cents; never negative
    Item.java               record: name, unit price, quantity
    TaxPolicy.java          interface: tax owed on a subtotal
    FlatTax.java            flat rate in basis points (implements TaxPolicy)
    Cart.java               add/remove items, subtotal, tax, total
    Receipt.java            plain-text receipt formatting
    Main.java               prints a sample receipt
  test/demo/
    TestRunner.java         reflection runner: *Test classes, public test* methods
    Check.java              assertion helpers that throw AssertionError
    MoneyTest.java
    CartTest.java
    ReceiptTest.java
```

Tests are plain public methods named `test*` in classes whose names end with `Test`.
Each method is independent and builds its own fixtures.

## Known failing test

`demo.CartTest.testTaxRoundsHalfUp` fails on purpose. It is the deliberate bug for the
editor's debug flow, so do not fix it here.

- Test: a single 105-cent item with a 10% (1000 bps) flat tax. The tax is 10.5 cents,
  which must round half up to 11 cents. The test expects 11.
- Cause: `FlatTax.taxOn` truncates with integer division instead of rounding half up,
  so it returns 10.
- Expected output: `FAIL demo.CartTest.testTaxRoundsHalfUp: AssertionError: expected <$0.11> but was <$0.10>`

With this bug present, `build.sh` reports 18 passed and 1 failed, and exits 1.
