library pub.pub_package_provider;
import 'dart:async';
import 'package:barback/barback.dart';
import 'package:path/path.dart' as path;
import '../io.dart';
import '../package_graph.dart';
import '../preprocess.dart';
import '../sdk.dart' as sdk;
import '../utils.dart';
class PubPackageProvider implements PackageProvider {
  final PackageGraph _graph;
  final List<String> packages;
  PubPackageProvider(PackageGraph graph, [Iterable<String> packages])
      : _graph = graph,
        packages = [
          r"$pub",
          r"$sdk"]..addAll(packages == null ? graph.packages.keys : packages);
  Future<Asset> getAsset(AssetId id) {
    if (id.package == r'$pub') {
      var components = path.url.split(id.path);
      assert(components.isNotEmpty);
      assert(components.first == 'lib');
      components[0] = 'dart';
      var file = assetPath(path.joinAll(components));
      if (!_graph.packages.containsKey("barback")) {
        return new Future.value(new Asset.fromPath(id, file));
      }
      var versions =
          mapMap(_graph.packages, value: (_, package) => package.version);
      var contents = readTextFile(file);
      contents = preprocess(contents, versions, path.toUri(file));
      return new Future.value(new Asset.fromString(id, contents));
    }
    if (id.package == r'$sdk') {
      var parts = path.split(path.fromUri(id.path));
      assert(parts.isNotEmpty && parts[0] == 'lib');
      parts = parts.skip(1);
      var file = path.join(sdk.rootDirectory, path.joinAll(parts));
      return new Future.value(new Asset.fromPath(id, file));
    }
    var nativePath = path.fromUri(id.path);
    var file = path.join(_graph.packages[id.package].dir, nativePath);
    return new Future.value(new Asset.fromPath(id, file));
  }
}
