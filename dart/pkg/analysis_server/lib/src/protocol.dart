// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library protocol;

import 'dart:collection';
import 'dart:convert' show JsonDecoder;

import 'package:analysis_server/src/services/json.dart';

/**
 * Instances of the class [Request] represent a request that was received.
 */
class Request {
  /**
   * The name of the JSON attribute containing the id of the request.
   */
  static const String ID = 'id';

  /**
   * The name of the JSON attribute containing the name of the request.
   */
  static const String METHOD = 'method';

  /**
   * The name of the JSON attribute containing the request parameters.
   */
  static const String PARAMS = 'params';

  /**
   * The unique identifier used to identify this request.
   */
  final String id;

  /**
   * The method being requested.
   */
  final String method;

  /**
   * A table mapping the names of request parameters to their values.
   */
  final Map<String, Object> params;

  /**
   * A decoder that can be used to decode strings into JSON objects.
   */
  static const JsonDecoder DECODER = const JsonDecoder(null);

  /**
   * Initialize a newly created [Request] to have the given [id] and [method]
   * name.  If [params] is supplied, it is used as the "params" map for the
   * request.  Otherwise an empty "params" map is allocated.
   */
  Request(this.id, this.method, [Map<String, Object> params])
      : params = params != null ? params : new HashMap<String, Object>();

  /**
   * Return a request parsed from the given [data], or `null` if the [data] is
   * not a valid json representation of a request. The [data] is expected to
   * have the following format:
   *
   *   {
   *     'id': String,
   *     'method': methodName,
   *     'params': {
   *       paramter_name: value
   *     }
   *   }
   *
   * where the parameters are optional and can contain any number of name/value
   * pairs.
   */
  factory Request.fromString(String data) {
    try {
      var result = DECODER.convert(data);
      if (result is! Map) {
        return null;
      }
      var id = result[Request.ID];
      var method = result[Request.METHOD];
      if (id is! String || method is! String) {
        return null;
      }
      var params = result[Request.PARAMS];
      if (params is Map || params == null) {
        return new Request(id, method, params);
      } else {
        return null;
      }
    } catch (exception) {
      return null;
    }
  }

  /**
   * Return a table representing the structure of the Json object that will be
   * sent to the client to represent this response.
   */
  Map<String, Object> toJson() {
    Map<String, Object> jsonObject = new HashMap<String, Object>();
    jsonObject[ID] = id;
    jsonObject[METHOD] = method;
    if (params.isNotEmpty) {
      jsonObject[PARAMS] = params;
    }
    return jsonObject;
  }
}

/**
 * Instances of the class [Response] represent a response to a request.
 */
class Response {
  /**
   * The [Response] instance that is returned when a real [Response] cannot
   * be provided at the moment.
   */
  static final Response DELAYED_RESPONSE = new Response('DELAYED_RESPONSE');

  /**
   * The name of the JSON attribute containing the id of the request for which
   * this is a response.
   */
  static const String ID = 'id';

  /**
   * The name of the JSON attribute containing the error message.
   */
  static const String ERROR = 'error';

  /**
   * The name of the JSON attribute containing the result values.
   */
  static const String RESULT = 'result';

  /**
   * The unique identifier used to identify the request that this response is
   * associated with.
   */
  final String id;

  /**
   * The error that was caused by attempting to handle the request, or `null` if
   * there was no error.
   */
  final RequestError error;

  /**
   * A table mapping the names of result fields to their values.  Should be
   * null if there is no result to send.
   */
  Map<String, Object> result;

  /**
   * Initialize a newly created instance to represent a response to a request
   * with the given [id].  If [result] is provided, it will be used as the
   * result; otherwise an empty result will be used.  If an [error] is provided
   * then the response will represent an error condition.
   */
  Response(this.id, {this.result, this.error});

  /**
   * Initialize a newly created instance to represent an error condition caused
   * by a [request] referencing a context that does not exist.
   */
  Response.contextDoesNotExist(Request request)
    : this(request.id, error: new RequestError('NONEXISTENT_CONTEXT', 'Context does not exist'));

  /**
   * Initialize a newly created instance to represent an error condition caused
   * by a [request] that had invalid parameter.  [path] is the path to the
   * invalid parameter, in Javascript notation (e.g. "foo.bar" means that the
   * parameter "foo" contained a key "bar" whose value was the wrong type).
   * [expectation] is a description of the type of data that was expected.
   */
  Response.invalidParameter(Request request, String path, String expectation)
      : this(request.id, error: new RequestError('INVALID_PARAMETER',
          "Expected parameter $path to $expectation"));

  /**
   * Initialize a newly created instance to represent an error condition caused
   * by a malformed request.
   */
  Response.invalidRequestFormat()
    : this('', error: new RequestError('INVALID_REQUEST', 'Invalid request'));

  /**
   * Initialize a newly created instance to represent an error condition caused
   * by a [request] that does not have a required parameter.
   */
  Response.missingRequiredParameter(Request request, String parameterName)
    : this(request.id, error: new RequestError('MISSING_PARAMETER', 'Missing required parameter: $parameterName'));

  /**
   * Initialize a newly created instance to represent an error condition caused
   * by a [request] that takes a set of analysis options but for which an
   * unknown analysis option was provided.
   */
  Response.unknownAnalysisOption(Request request, String optionName)
    : this(request.id, error: new RequestError('UNKNOWN_ANALYSIS_OPTION', 'Unknown analysis option: "$optionName"'));

  /**
   * Initialize a newly created instance to represent an error condition caused
   * by a [request] that cannot be handled by any known handlers.
   */
  Response.unknownRequest(Request request)
    : this(request.id, error: new RequestError('UNKNOWN_REQUEST', 'Unknown request'));

  Response.contextAlreadyExists(Request request)
    : this(request.id, error: new RequestError('CONTENT_ALREADY_EXISTS', 'Context already exists'));

  Response.unsupportedFeature(String requestId, String message)
    : this(requestId, error: new RequestError('UNSUPPORTED_FEATURE', message));

  /**
   * Initialize a newly created instance to represent an error condition caused
   * by a `analysis.setSubscriptions` [request] that includes an unknown
   * analysis service name.
   */
  Response.unknownAnalysisService(Request request, String name)
    : this(request.id, error: new RequestError('UNKNOWN_ANALYSIS_SERVICE', 'Unknown analysis service: "$name"'));

  /**
   * Initialize a newly created instance to represent an error condition caused
   * by a `analysis.setPriorityFiles` [request] that includes one or more files
   * that are not being analyzed.
   */
  Response.unanalyzedPriorityFiles(Request request, String fileNames)
    : this(request.id, error: new RequestError('UNANALYZED_PRIORITY_FILES', "Unanalyzed files cannot be a priority: '$fileNames'"));

  /**
   * Initialize a newly created instance to represent an error condition caused
   * by a `analysis.updateOptions` [request] that includes an unknown analysis
   * option.
   */
  Response.unknownOptionName(Request request, String optionName)
    : this(request.id, error: new RequestError('UNKNOWN_OPTION_NAME', 'Unknown analysis option: "$optionName"'));

  /**
   * Initialize a newly created instance to represent an error condition caused
   * by an error during `analysis.getErrors`.
   */
  Response.getErrorsError(Request request, String message,
      Map<String, Object> result)
    : this(
        request.id,
        error: new RequestError('GET_ERRORS_ERROR', 'Error during `analysis.getErrors`: $message.'),
        result: result);

  /**
   * Initialize a newly created instance based upon the given JSON data
   */
  factory Response.fromJson(Map<String, Object> json) {
    try {
      Object id = json[Response.ID];
      if (id is! String) {
        return null;
      }
      Object error = json[Response.ERROR];
      RequestError decodedError;
      if (error is Map) {
        decodedError = new RequestError.fromJson(error);
      }
      Object result = json[Response.RESULT];
      Map<String, Object> decodedResult;
      if (result is Map) {
        decodedResult = result;
      }
      return new Response(id, error: decodedError,
          result: decodedResult);
    } catch (exception) {
      return null;
    }
  }

  /**
   * Return a table representing the structure of the Json object that will be
   * sent to the client to represent this response.
   */
  Map<String, Object> toJson() {
    Map<String, Object> jsonObject = new HashMap<String, Object>();
    jsonObject[ID] = id;
    if (error != null) {
      jsonObject[ERROR] = error.toJson();
    }
    if (result != null) {
      jsonObject[RESULT] = result;
    }
    return jsonObject;
  }
}

/**
 * Instances of the class [RequestError] represent information about an error
 * that occurred while attempting to respond to a [Request].
 */
class RequestError {
  /**
   * The name of the JSON attribute containing the code that uniquely identifies
   * the error that occurred.
   */
  static const String CODE = 'code';

  /**
   * The name of the JSON attribute containing an object with additional data
   * related to the error.
   */
  static const String DATA = 'data';

  /**
   * The name of the JSON attribute containing a short description of the error.
   */
  static const String MESSAGE = 'message';

  /**
   * An error code indicating a parse error. Invalid JSON was received by the
   * server. An error occurred on the server while parsing the JSON text.
   */
  static const String CODE_PARSE_ERROR = 'PARSE_ERROR';

  /**
   * An error code indicating that the analysis server has already been
   * started (and hence won't accept new connections).
   */
  static const String CODE_SERVER_ALREADY_STARTED = 'SERVER_ALREADY_STARTED';

  /**
   * An error code indicating an invalid request. The JSON sent is not a valid
   * [Request] object.
   */
  static const String CODE_INVALID_REQUEST = 'INVALID_REQUEST';

  /**
   * An error code indicating a method not found. The method does not exist or
   * is not currently available.
   */
  static const String CODE_METHOD_NOT_FOUND = 'METHOD_NOT_FOUND';

  /**
   * An error code indicating one or more invalid parameters.
   */
  static const String CODE_INVALID_PARAMS = 'INVALID_PARAMS';

  /**
   * An error code indicating an internal error.
   */
  static const String CODE_INTERNAL_ERROR = 'INTERNAL_ERROR';

  /**
   * An error code indicating a problem using the specified Dart SDK.
   */
  static const String CODE_SDK_ERROR = 'SDK_ERROR';

  /**
   * An error code indicating a problem during 'analysis.getErrors'.
   */
  static const String CODE_ANALISYS_GET_ERRORS_ERROR = 'ANALYSIS_GET_ERRORS_ERROR';

  /**
   * The code that uniquely identifies the error that occurred.
   */
  final String code;

  /**
   * A short description of the error.
   */
  final String message;

  /**
   * A table mapping the names of notification parameters to their values.
   */
  final Map<String, Object> data = new HashMap<String, Object>();

  /**
   * Initialize a newly created [Error] to have the given [code] and [message].
   */
  RequestError(this.code, this.message);

  /**
   * Initialize a newly created [Error] to indicate a parse error. Invalid JSON
   * was received by the server. An error occurred on the server while parsing
   * the JSON text.
   */
  RequestError.parseError() : this(CODE_PARSE_ERROR, "Parse error");

  /**
   * Initialize a newly created [Error] to indicate that the analysis server
   * has already been started (and hence won't accept new connections).
   */
  RequestError.serverAlreadyStarted()
    : this(CODE_SERVER_ALREADY_STARTED, "Server already started");

  /**
   * Initialize a newly created [Error] to indicate an invalid request. The
   * JSON sent is not a valid [Request] object.
   */
  RequestError.invalidRequest() : this(CODE_INVALID_REQUEST, "Invalid request");

  /**
   * Initialize a newly created [Error] to indicate that a method was not found.
   * Either the method does not exist or is not currently available.
   */
  RequestError.methodNotFound() : this(CODE_METHOD_NOT_FOUND, "Method not found");

  /**
   * Initialize a newly created [Error] to indicate one or more invalid
   * parameters.
   */
  RequestError.invalidParameters() : this(CODE_INVALID_PARAMS, "Invalid parameters");

  /**
   * Initialize a newly created [Error] to indicate an internal error.
   */
  RequestError.internalError() : this(CODE_INTERNAL_ERROR, "Internal error");

  /**
   * Initialize a newly created [Error] from the given JSON.
   */
  factory RequestError.fromJson(Map<String, Object> json) {
    try {
      String code = json[RequestError.CODE];
      String message = json[RequestError.MESSAGE];
      Map<String, Object> data = json[RequestError.DATA];
      RequestError requestError = new RequestError(code, message);
      if (data != null) {
        data.forEach((String key, Object value) {
          requestError.setData(key, value);
        });
      }
      return requestError;
    } catch (exception) {
      return null;
    }
  }

  /**
   * Return the value of the data with the given [name], or `null` if there is
   * no such data associated with this error.
   */
  Object getData(String name) => data[name];

  /**
   * Set the value of the data with the given [name] to the given [value].
   */
  void setData(String name, Object value) {
    data[name] = value;
  }

  /**
   * Return a table representing the structure of the Json object that will be
   * sent to the client to represent this response.
   */
  Map<String, Object> toJson() {
    Map<String, Object> jsonObject = new HashMap<String, Object>();
    jsonObject[CODE] = code;
    jsonObject[MESSAGE] = message;
    if (!data.isEmpty) {
      jsonObject[DATA] = data;
    }
    return jsonObject;
  }

  @override
  String toString() => toJson().toString();
}

/**
 * Instances of the class [Notification] represent a notification from the
 * server about an event that occurred.
 */
class Notification {
  /**
   * The name of the JSON attribute containing the name of the event that
   * triggered the notification.
   */
  static const String EVENT = 'event';

  /**
   * The name of the JSON attribute containing the result values.
   */
  static const String PARAMS = 'params';

  /**
   * The name of the event that triggered the notification.
   */
  final String event;

  /**
   * A table mapping the names of notification parameters to their values, or
   * null if there are no notification parameters.
   */
  Map<String, Object> params;

  /**
   * Initialize a newly created [Notification] to have the given [event] name.
   * If [params] is provided, it will be used as the params; otherwise no
   * params will be used.
   */
  Notification(this.event, [this.params]);

  /**
   * Initialize a newly created instance based upon the given JSON data
   */
  factory Notification.fromJson(Map<String, Object> json) {
    try {
      String event = json[Notification.EVENT];
      Object params = json[Notification.PARAMS];
      Notification notification = new Notification(event);
      if (params is Map) {
        params.forEach((String key, Object value) {
          notification.setParameter(key, value);
        });
      }
      return notification;
    } catch (exception) {
      return null;
    }
  }

  /**
   * Set the value of the parameter with the given [name] to the given [value].
   */
  void setParameter(String name, Object value) {
    if (params == null) {
      params = new HashMap<String, Object>();
    }
    params[name] = _toJson(value);
  }

  /**
   * Return a table representing the structure of the Json object that will be
   * sent to the client to represent this response.
   */
  Map<String, Object> toJson() {
    Map<String, Object> jsonObject = {};
    jsonObject[EVENT] = event;
    if (params != null) {
      jsonObject[PARAMS] = params;
    }
    return jsonObject;
  }
}

/**
 * Instances of the class [RequestHandler] implement a handler that can handle
 * requests and produce responses for them.
 */
abstract class RequestHandler {
  /**
   * Attempt to handle the given [request]. If the request is not recognized by
   * this handler, return `null` so that other handlers will be given a chance
   * to handle it. Otherwise, return the response that should be passed back to
   * the client.
   */
  Response handleRequest(Request request);
}

/**
 * Instances of the class [RequestFailure] represent an exception that occurred
 * during the handling of a request that requires that an error be returned to
 * the client.
 */
class RequestFailure implements Exception {
  /**
   * The response to be returned as a result of the failure.
   */
  final Response response;

  /**
   * Initialize a newly created exception to return the given reponse.
   */
  RequestFailure(this.response);
}

/**
 * Returns a JSON presention of [value].
 */
_toJson(Object value) {
  if (value is HasToJson) {
    return value.toJson();
  }
  if (value is Iterable) {
    return value.map((item) => _toJson(item)).toList();
  }
  return value;
}
