// swift-tools-version:5.9
// The SPM manifest fixture (SPRINT-23): the declared-fact resolver's
// module channel -- five targets with EXPLICIT paths; the manifest file
// itself carries no module and stays in the OC006 set.
import PackageDescription

let package = Package(
    name: "Fixtures",
    targets: [
        .target(name: "Core", path: "Core"),
        .target(name: "Widgets", path: "Widgets"),
        .target(name: "Tools", path: "Tools"),
        .target(name: "Kit", path: "Kit"),
        .target(name: "App", path: "App")
    ]
)
