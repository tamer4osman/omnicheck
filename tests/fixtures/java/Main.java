// Corpus A entry-point fixture (SPRINT-11): the pack entry-point rule
// ("Main.java") claims this file by the documented file-name
// approximation (docs/LANGUAGES.md §Java.6); 'public static void main'
// claiming is reserved for a resolver sprint.
//
// Both imports name classes OUTSIDE the analyzed set (the JDK and an
// external artifact). Under resolver v1-java they stay unresolved -- an
// edge always means "the target is part of this analysis", never a guess
// (docs/LANGUAGES.md §Java.5). Positive resolution is pinned by
// tests/fixtures/java_resolver.
import java.util.List;
import org.apache.commons.io.FileUtils;

public class Main {

    public List<String> run(List<String> input) {
        FileUtils.quietDelete(input);
        return input;
    }
}
