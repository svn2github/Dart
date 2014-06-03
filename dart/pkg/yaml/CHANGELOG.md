## 1.0.0

* **Backwards incompatibility**: The data structures returned by `loadYaml` and
  `loadYamlStream` are now immutable.

* **Backwards incompatibility**: The interface of the `YamlMap` class has
  changed substantially in numerous ways. External users may no longer construct
  their own instances.

* Maps and lists returned by `loadYaml` and `loadYamlStream` now contain
  information about their source locations.

* A new `loadYamlNode` function returns the source location of top-level scalars
  as well.

## 0.10.0

* Improve error messages when a file fails to parse.

## 0.9.0+2

* Ensure that maps are order-independent when used as map keys.

## 0.9.0+1

* The `YamlMap` class is deprecated. In a future version, maps returned by
  `loadYaml` and `loadYamlStream` will be Dart `HashMap`s with a custom equality
  operation.
