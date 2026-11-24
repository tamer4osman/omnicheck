// Resolver corpus B (SPRINT-23): the manifest declares five targets --
// Store, Lib, Wide, Alpha and Beta with EXPLICIT paths, and Std with NO
// path argument: the SPM documented default Sources/<name> applies (the
// default-path rule proven live).
// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "Resolver",
    targets: [
        .target(name: "Store", path: "Store"),
        .target(name: "Lib", path: "Lib"),
        .target(name: "Wide", path: "Wide"),
        .target(name: "Alpha", path: "Alpha"),
        .target(name: "Beta", path: "Beta"),
        .target(name: "Std")
    ]
)
