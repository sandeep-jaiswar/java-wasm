package demo;

/** One flat rate applied to the whole subtotal, given in basis points (1/100 of a percent). */
public record FlatTax(int rateBasisPoints) implements TaxPolicy {

    public FlatTax {
        if (rateBasisPoints < 0 || rateBasisPoints > 10_000) {
            throw new IllegalArgumentException("rate must be 0..10000 basis points: " + rateBasisPoints);
        }
    }

    @Override
    public Money taxOn(Money subtotal) {
        return Money.ofCents(subtotal.cents() * rateBasisPoints / 10_000);
    }
}
