package demo;

/** Renders a cart as a plain-text receipt. */
public final class Receipt {

    private Receipt() {
    }

    public static String render(Cart cart) {
        StringBuilder out = new StringBuilder();
        for (Item item : cart.items()) {
            out.append(String.format("%-12s %2d x %8s %9s%n",
                    item.name(), item.quantity(), item.unitPrice(), item.lineTotal()));
        }
        out.append(line("Subtotal", cart.subtotal()));
        out.append(line("Tax", cart.tax()));
        out.append(line("Total", cart.total()));
        return out.toString();
    }

    private static String line(String label, Money amount) {
        return String.format("%-12s %20s%n", label, amount);
    }
}
