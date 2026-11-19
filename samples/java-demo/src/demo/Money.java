package demo;

/** An amount of money in whole cents. Amounts are never negative. */
public final class Money implements Comparable<Money> {
    public static final Money ZERO = new Money(0);

    private final long cents;

    private Money(long cents) {
        this.cents = cents;
    }

    public static Money ofCents(long cents) {
        if (cents < 0) {
            throw new IllegalArgumentException("money cannot be negative: " + cents);
        }
        return new Money(cents);
    }

    public long cents() {
        return cents;
    }

    public Money plus(Money other) {
        return new Money(Math.addExact(cents, other.cents));
    }

    /** Subtracts {@code other}; the result must not be negative. */
    public Money minus(Money other) {
        return ofCents(cents - other.cents);
    }

    public Money times(int quantity) {
        if (quantity < 0) {
            throw new IllegalArgumentException("quantity cannot be negative: " + quantity);
        }
        return new Money(Math.multiplyExact(cents, (long) quantity));
    }

    @Override
    public int compareTo(Money other) {
        return Long.compare(cents, other.cents);
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof Money m && m.cents == cents;
    }

    @Override
    public int hashCode() {
        return Long.hashCode(cents);
    }

    @Override
    public String toString() {
        return String.format("$%d.%02d", cents / 100, cents % 100);
    }
}
