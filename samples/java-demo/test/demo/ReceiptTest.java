package demo;

public final class ReceiptTest {

    private static Cart sampleCart() {
        Cart cart = new Cart(new FlatTax(800));
        cart.add(new Item("Pen", Money.ofCents(125), 2));
        cart.add(new Item("Notebook", Money.ofCents(400), 1));
        return cart;
    }

    public void testEachItemAppearsWithItsLineTotal() {
        String receipt = Receipt.render(sampleCart());
        Check.contains(receipt, "Pen");
        Check.contains(receipt, "Notebook");
        Check.contains(receipt, "$2.50");
        Check.contains(receipt, "$4.00");
    }

    public void testTotalLineShowsGrandTotal() {
        String receipt = Receipt.render(sampleCart());
        String[] lines = receipt.split("\n");
        String last = lines[lines.length - 1];
        Check.isTrue(last.startsWith("Total"), "last line should be Total but was: " + last);
        Check.isTrue(last.endsWith("$7.02"), "total should end with $7.02 but was: " + last);
    }

    public void testSubtotalAndTaxLinesArePresent() {
        String receipt = Receipt.render(sampleCart());
        Check.contains(receipt, "Subtotal");
        Check.contains(receipt, "$6.50");
        Check.contains(receipt, "Tax");
        Check.contains(receipt, "$0.52");
    }

    public void testEmptyCartRendersZeroTotals() {
        String receipt = Receipt.render(new Cart(new FlatTax(800)));
        Check.contains(receipt, "Total");
        Check.contains(receipt, "$0.00");
    }
}
