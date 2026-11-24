// Resolver corpus B entry fixture (SPRINT-11). The import set pins every
// v1-java semantics branch of docs/LANGUAGES.md §Java.5:
//   - com.example.store.Store      single-type  -> resolves (Store.java)
//   - com.example.store.Types      single-type  -> resolves (Types.java)
//   - static com.example.store.Store.HELLO -> qualifier resolves (Store.java)
//   - com.example.store.*          wildcard     -> package claim: fans out to
//     Store.java, Types.java AND StoreTest.java (a Java wildcard claims the
//     whole package; unlike Go there is no test-file exclusion)
//   - com.example.util.*           wildcard     -> package claim (Helper.java)
//   - com.example.deep.Nested.Inner  nested-class import: a/b/C/D.java does
//     NOT exist and v1 never falls back to Nested.java -> UNRESOLVED
//   - java.util.ArrayList          JDK          -> unresolved
//   - com.example.ghost.Thing      decoy named ONLY by the pom.xml (which the
//     resolver never reads; the directory does not exist) -> unresolved
package app;

import com.example.deep.Nested.Inner;
import com.example.store.Store;
import com.example.store.Types;
import com.example.store.StoreTest;

import static com.example.store.Store.HELLO;

import com.example.store.*;
import com.example.util.*;

import java.util.ArrayList;
import com.example.ghost.Thing;

public class Main {

    public String run() {
        Store s = new Store();
        Types t = new Types();
        StoreTest st = new StoreTest();
        Helper h = new Helper();
        System.out.print(HELLO);
        System.out.print(new ArrayList<String>().size());
        return s.name() + t.name() + st.name() + h.name() + Inner.NAME;
    }
}
