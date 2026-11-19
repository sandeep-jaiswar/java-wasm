package demo;

public final class CartTest {

    private static Cart cartWithFlatTax(int basisPoints) {
        return new Cart(new FlatTax(basisPoints));
    }

    public void testEmptyCartTotalIsZero() {
        Cart cart = cartWithFlatTax(800);
        Check.isTrue(cart.isEmpty(), "new cart should be empty");
        Check.equal(Money.ZERO, cart.total());
    }

    public void testSubtotalSumsLineTotals() {
        Cart cart = cartWithFlatTax(0);
        cart.add(new Item("Pen", Money.ofCents(125), 2));
        cart.add(new Item("Notebook", Money.ofCents(400), 1));
        Check.equal(Money.ofCents(650), cart.subtotal());
    }

    public void testAddingSameItemMergesQuantity() {
        Cart cart = cartWithFlatTax(0);
        cart.add(new Item("Pen", Money.ofCents(125), 2));
        cart.add(new Item("Pen", Money.ofCents(125), 3));
        Check.equal(1, cart.items().size());
        Check.equal(5, cart.items().get(0).quantity());
    }

    public void testAddingSameItemWithDifferentPriceIsRejected() {
        Cart cart = cartWithFlatTax(0);
        cart.add(new Item("Pen", Money.ofCents(125), 1));
        Check.throwsException(IllegalArgumentException.class,
                () -> cart.add(new Item("Pen", Money.ofCents(130), 1)));
    }

    public void testRemoveDropsTheLine() {
        Cart cart = cartWithFlatTax(0);
        cart.add(new Item("Pen", Money.ofCents(125), 1));
        cart.remove("Pen");
        Check.isTrue(cart.isEmpty(), "cart should be empty after remove");
    }

    public void testRemoveUnknownItemThrows() {
        Cart cart = cartWithFlatTax(0);
        Check.throwsException(java.util.NoSuchElementException.class, () -> cart.remove("Ghost"));
    }

    public void testTotalIncludesFlatTax() {
        Cart cart = cartWithFlatTax(800);
        cart.add(new Item("Pen", Money.ofCents(125), 2));
        cart.add(new Item("Notebook", Money.ofCents(400), 1));
        Check.equal(Money.ofCents(52), cart.tax());
        Check.equal(Money.ofCents(702), cart.total());
    }

    public void testTaxComesFromInjectedPolicy() {
        TaxPolicy sevenCents = subtotal -> Money.ofCents(7);
        Cart cart = new Cart(sevenCents);
        cart.add(new Item("Pen", Money.ofCents(100), 1));
        Check.equal(Money.ofCents(7), cart.tax());
        Check.equal(Money.ofCents(107), cart.total());
    }

    /**
     * KNOWN FAILING TEST. 105 cents at 10% is 10.5 cents, which must round half up to 11.
     * FlatTax currently truncates to 10. See README "Known failing test".
     */
    public void testTaxRoundsHalfUp() {
        Cart cart = cartWithFlatTax(1000);
        cart.add(new Item("Pen", Money.ofCents(105), 1));
        Check.equal(Money.ofCents(11), cart.tax());
    }
}
