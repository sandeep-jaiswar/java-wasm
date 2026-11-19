package demo;

/** Decides how much tax a cart owes for a given subtotal. */
public interface TaxPolicy {
    Money taxOn(Money subtotal);
}
