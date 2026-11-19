package demo;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.Objects;

/** A shopping cart. Lines are keyed by item name and keep insertion order. */
public final class Cart {
    private final Map<String, Item> items = new LinkedHashMap<>();
    private final TaxPolicy taxPolicy;

    public Cart(TaxPolicy taxPolicy) {
        this.taxPolicy = Objects.requireNonNull(taxPolicy, "taxPolicy");
    }

    /** Adds an item. Adding a name that is already present sums the quantities. */
    public void add(Item item) {
        Item existing = items.get(item.name());
        if (existing == null) {
            items.put(item.name(), item);
            return;
        }
        if (!existing.unitPrice().equals(item.unitPrice())) {
            throw new IllegalArgumentException("price mismatch for " + item.name());
        }
        items.put(item.name(), new Item(item.name(), item.unitPrice(),
                existing.quantity() + item.quantity()));
    }

    /** Removes the whole line for {@code name}. */
    public void remove(String name) {
        if (items.remove(name) == null) {
            throw new NoSuchElementException("no item named " + name);
        }
    }

    public List<Item> items() {
        return List.copyOf(items.values());
    }

    public boolean isEmpty() {
        return items.isEmpty();
    }

    public Money subtotal() {
        Money sum = Money.ZERO;
        for (Item item : items.values()) {
            sum = sum.plus(item.lineTotal());
        }
        return sum;
    }

    public Money tax() {
        return taxPolicy.taxOn(subtotal());
    }

    public Money total() {
        return subtotal().plus(tax());
    }
}
