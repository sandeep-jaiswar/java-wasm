package demo;

public final class MoneyTest {

    public void testRejectsNegativeAmounts() {
        Check.throwsException(IllegalArgumentException.class, () -> Money.ofCents(-1));
    }

    public void testPlusAndMinus() {
        Money sum = Money.ofCents(150).plus(Money.ofCents(25));
        Check.equal(Money.ofCents(175), sum);
        Check.equal(Money.ofCents(125), sum.minus(Money.ofCents(50)));
    }

    public void testMinusBelowZeroIsRejected() {
        Check.throwsException(IllegalArgumentException.class,
                () -> Money.ofCents(10).minus(Money.ofCents(11)));
    }

    public void testTimesMultipliesByQuantity() {
        Check.equal(Money.ofCents(375), Money.ofCents(125).times(3));
    }

    public void testToStringAlwaysShowsTwoDecimals() {
        Check.equal("$0.05", Money.ofCents(5).toString());
        Check.equal("$12.30", Money.ofCents(1230).toString());
    }

    public void testZeroIsZero() {
        Check.equal(Money.ofCents(0), Money.ZERO);
        Check.equal(0L, Money.ZERO.cents());
    }
}
