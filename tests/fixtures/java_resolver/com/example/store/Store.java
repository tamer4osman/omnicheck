// Resolver corpus B: the cycle pair. Store imports Types and Types
// imports Store (legal Java: same-package types may be imported
// explicitly) -- OC003 pins the 2-file cycle.
package com.example.store;

import com.example.store.Types;

public class Store {

    public static final String HELLO = "hello";

    public String name() {
        return Types.label();
    }
}
