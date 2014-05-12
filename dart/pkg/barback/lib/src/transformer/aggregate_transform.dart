// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library barback.transformer.aggregate_transform;

import 'dart:async';
import 'dart:convert';

import '../asset/asset.dart';
import '../asset/asset_id.dart';
import '../asset/asset_set.dart';
import '../errors.dart';
import '../graph/transform_node.dart';
import '../utils.dart';
import 'base_transform.dart';

/// A transform for [AggregateTransformer]s that provides access to all of their
/// primary inputs.
class AggregateTransform extends BaseTransform {
  final TransformNode _node;

  /// The set of outputs emitted by the transformer.
  final _outputs = new AssetSet();

  /// The transform key.
  ///
  /// This is the key returned by [AggregateTransformer.classifyPrimary] for all
  /// the assets in this transform.
  String get key => _node.key;

  /// The stream of primary inputs that will be processed by this transform.
  ///
  /// This is exposed as a stream so that the transformer can start working
  /// before all its inputs are available. The stream is closed not just when
  /// all inputs are provided, but when barback is confident no more inputs will
  /// be forthcoming.
  ///
  /// A transformer may complete its `apply` method before this stream is
  /// closed. For example, it may know that each key will only have two inputs
  /// associated with it, and so use `transform.primaryInputs.take(2)` to access
  /// only those inputs.
  Stream<Asset> get primaryInputs => _primaryInputs;
  Stream<Asset> _primaryInputs;

  /// The controller for [primaryInputs].
  ///
  /// This is a broadcast controller so that the transform can keep
  /// [_emittedPrimaryInputs] up to date.
  final _inputController = new StreamController<Asset>.broadcast();

  /// The set of all primary inputs that have been emitted by [primaryInputs].
  final _emittedPrimaryInputs = new AssetSet();

  AggregateTransform._(TransformNode node)
      : _node = node,
        super(node) {
    _inputController.stream.listen(_emittedPrimaryInputs.add);
    // [primaryInputs] should be a non-broadcast stream.
    _primaryInputs = broadcastToSingleSubscription(_inputController.stream);
  }

  /// Gets the asset for an input [id].
  ///
  /// If an input with [id] cannot be found, throws an [AssetNotFoundException].
  Future<Asset> getInput(AssetId id) {
    if (_emittedPrimaryInputs.containsId(id)) {
      return syncFuture(() => _emittedPrimaryInputs[id]);
    } else {
      return _node.getInput(id);
    }
  }

  /// A convenience method to the contents of the input with [id] as a string.
  ///
  /// This is equivalent to calling [getInput] followed by [Asset.readAsString].
  ///
  /// If the asset was created from a [String] the original string is always
  /// returned and [encoding] is ignored. Otherwise, the binary data of the
  /// asset is decoded using [encoding], which defaults to [UTF8].
  ///
  /// If an input with [id] cannot be found, throws an [AssetNotFoundException].
  Future<String> readInputAsString(AssetId id, {Encoding encoding}) {
    if (encoding == null) encoding = UTF8;
    return getInput(id).then((input) => input.readAsString(encoding: encoding));
  }

  /// A convenience method to the contents of the input with [id].
  ///
  /// This is equivalent to calling [getInput] followed by [Asset.read].
  ///
  /// If the asset was created from a [String], this returns its UTF-8 encoding.
  ///
  /// If an input with [id] cannot be found, throws an [AssetNotFoundException].
  Stream<List<int>> readInput(AssetId id) =>
      futureStream(getInput(id).then((input) => input.read()));

  /// A convenience method to return whether or not an asset exists.
  ///
  /// This is equivalent to calling [getInput] and catching an
  /// [AssetNotFoundException].
  Future<bool> hasInput(AssetId id) {
    return getInput(id).then((_) => true).catchError((error) {
      if (error is AssetNotFoundException && error.id == id) return false;
      throw error;
    });
  }

  /// Stores [output] as the output created by this transformation.
  ///
  /// A transformation can output as many assets as it wants.
  void addOutput(Asset output) {
    // TODO(rnystrom): This should immediately throw if an output with that ID
    // has already been created by this transformer.
    _outputs.add(output);
  }

  void consumePrimary(AssetId id) {
    if (!_emittedPrimaryInputs.containsId(id)) {
      throw new StateError(
          "$id can't be consumed because it's not a primary input.");
    }

    super.consumePrimary(id);
  }
}

/// The controller for [AggregateTransform].
class AggregateTransformController extends BaseTransformController {
  AggregateTransform get transform => super.transform;

  /// The set of assets that the transformer has emitted.
  AssetSet get outputs => transform._outputs;

  /// The controller for the [AggregateTransform.primaryInputs] stream.
  StreamController<Asset> get inputController => transform._inputController;

  AggregateTransformController(TransformNode node)
      : super(new AggregateTransform._(node));

  void close() {
    super.close();
    transform._inputController.close();
  }
}