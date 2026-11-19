package demo;

import java.io.File;
import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.stream.Stream;

/**
 * Minimal reflection-based test runner. Finds classes named *Test on the classpath
 * directories, runs every public no-arg method whose name starts with "test", and
 * exits 1 if any test fails.
 */
public final class TestRunner {

    private static final String CLASS_SUFFIX = ".class";

    private TestRunner() {
    }

    public static void main(String[] args) throws IOException {
        int passed = 0;
        int failed = 0;
        for (String className : findTestClasses()) {
            Class<?> type;
            try {
                type = Class.forName(className);
            } catch (ClassNotFoundException e) {
                System.out.println("FAIL " + className + ": cannot load class");
                failed++;
                continue;
            }
            for (Method method : testMethods(type)) {
                String name = className + "." + method.getName();
                try {
                    invoke(type, method);
                    System.out.println("PASS " + name);
                    passed++;
                } catch (Throwable t) {
                    System.out.println("FAIL " + name + ": " + describe(t));
                    failed++;
                }
            }
        }
        System.out.println(passed + " passed, " + failed + " failed");
        System.exit(failed == 0 ? 0 : 1);
    }

    private static List<String> findTestClasses() throws IOException {
        List<String> names = new ArrayList<>();
        for (String entry : System.getProperty("java.class.path").split(File.pathSeparator)) {
            Path root = Path.of(entry);
            if (!Files.isDirectory(root)) {
                continue;
            }
            try (Stream<Path> files = Files.walk(root)) {
                files.filter(p -> isTestClassFile(p))
                        .map(p -> toClassName(root, p))
                        .forEach(names::add);
            }
        }
        names.sort(Comparator.naturalOrder());
        return names;
    }

    private static boolean isTestClassFile(Path p) {
        String fileName = p.getFileName().toString();
        return fileName.endsWith("Test.class") && !fileName.contains("$");
    }

    private static String toClassName(Path root, Path classFile) {
        String relative = root.relativize(classFile).toString();
        String withoutSuffix = relative.substring(0, relative.length() - CLASS_SUFFIX.length());
        return withoutSuffix.replace(File.separatorChar, '.');
    }

    private static List<Method> testMethods(Class<?> type) {
        return Arrays.stream(type.getMethods())
                .filter(m -> m.getName().startsWith("test"))
                .filter(m -> m.getParameterCount() == 0)
                .filter(m -> m.getDeclaringClass() == type)
                .sorted(Comparator.comparing(Method::getName))
                .toList();
    }

    private static void invoke(Class<?> type, Method method) throws Throwable {
        Object target = Modifier.isStatic(method.getModifiers())
                ? null
                : type.getConstructor().newInstance();
        try {
            method.invoke(target);
        } catch (InvocationTargetException e) {
            throw e.getCause();
        }
    }

    private static String describe(Throwable t) {
        return t.getClass().getSimpleName() + ": " + t.getMessage();
    }
}
