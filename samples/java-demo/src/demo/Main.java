package demo;

/** Prints a sample receipt. */
public final class Main {

    private Main() {
    }

    public static void main(String[] args) {
        Cart cart = new Cart(new FlatTax(800));
        cart.add(new Item("Pen", Money.ofCents(125), 2));
        cart.add(new Item("Notebook", Money.ofCents(400), 1));
        System.out.print(Receipt.render(cart));
    }
}
