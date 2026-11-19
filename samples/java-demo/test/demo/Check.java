package demo;

import java.util.Objects;

/** Tiny assertion helpers. Each failure throws AssertionError. */
final class Check {

    private Check() {
    }

    static void equal(Object expected, Object actual) {
        if (!Objects.equals(expected, actual)) {
            throw new AssertionError("expected <" + expected + "> but was <" + actual + ">");
        }
    }

    static void isTrue(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    static void contains(String text, String part) {
        if (!text.contains(part)) {
            throw new AssertionError("expected text to contain <" + part + "> but was:\n" + text);
        }
    }

    static void throwsException(Class<? extends Throwable> type, Runnable action) {
        try {
            action.run();
        } catch (Throwable t) {
            if (type.isInstance(t)) {
                return;
            }
            throw new AssertionError("expected " + type.getSimpleName() + " but got " + t, t);
        }
        throw new AssertionError("expected " + type.getSimpleName() + " but nothing was thrown");
    }
}
