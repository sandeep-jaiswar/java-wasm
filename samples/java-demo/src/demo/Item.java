package demo;

import java.util.Objects;

/** A product line: a name, a unit price and a positive quantity. */
public record Item(String name, Money unitPrice, int quantity) {

    public Item {
        if (name == null || name.isBlank()) {
            throw new IllegalArgumentException("name must not be blank");
        }
        Objects.requireNonNull(unitPrice, "unitPrice");
        if (quantity <= 0) {
            throw new IllegalArgumentException("quantity must be positive: " + quantity);
        }
    }

    public Money lineTotal() {
        return unitPrice.times(quantity);
    }
}
