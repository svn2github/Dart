// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

part of dart.io;

class FileSystemEntityType {
  static const FILE = const FileSystemEntityType._internal(0);
  static const DIRECTORY = const FileSystemEntityType._internal(1);
  static const LINK = const FileSystemEntityType._internal(2);
  static const NOT_FOUND = const FileSystemEntityType._internal(3);
  static const _typeList = const [FileSystemEntityType.FILE,
                                  FileSystemEntityType.DIRECTORY,
                                  FileSystemEntityType.LINK,
                                  FileSystemEntityType.NOT_FOUND];
  const FileSystemEntityType._internal(int this._type);

  static FileSystemEntityType _lookup(int type) => _typeList[type];
  String toString() => const ['FILE', 'DIRECTORY', 'LINK', 'NOT_FOUND'][_type];

  final int _type;
}

/**
 * A FileStat object represents the result of calling the POSIX stat() function
 * on a file system object.  It is an immutable object, representing the
 * snapshotted values returned by the stat() call.
 */
class FileStat {
  // These must agree with enum FileStat in file.h.
  static const _TYPE = 0;
  static const _CHANGED_TIME = 1;
  static const _MODIFIED_TIME = 2;
  static const _ACCESSED_TIME = 3;
  static const _MODE = 4;
  static const _SIZE = 5;

  FileStat._internal(this.changed,
                     this.modified,
                     this.accessed,
                     this.type,
                     this.mode,
                     this.size);

  external static List<int> _statSync(String path);


  /**
   * Calls the operating system's stat() function on [path].
   * Returns a [FileStat] object containing the data returned by stat().
   * If the call fails, returns a [FileStat] object with .type set to
   * FileSystemEntityType.NOT_FOUND and the other fields invalid.
   */
  static FileStat statSync(String path) {
    var data = _statSync(path);
    if (data is Error) throw data;
    return new FileStat._internal(
        new DateTime.fromMillisecondsSinceEpoch(data[_CHANGED_TIME] * 1000),
        new DateTime.fromMillisecondsSinceEpoch(data[_MODIFIED_TIME] * 1000),
        new DateTime.fromMillisecondsSinceEpoch(data[_ACCESSED_TIME] * 1000),
        FileSystemEntityType._lookup(data[_TYPE]),
        data[_MODE],
        data[_SIZE]);
  }

  /**
   * Asynchronously calls the operating system's stat() function on [path].
   * Returns a Future which completes with a [FileStat] object containing
   * the data returned by stat().
   * If the call fails, completes the future with a [FileStat] object with
   * .type set to FileSystemEntityType.NOT_FOUND and the other fields invalid.
   */
  static Future<FileStat> stat(String path) {
    // Get a new file service port for each request.  We could also cache one.
    var service = _FileUtils._newServicePort();
    List request = new List(2);
    request[0] = _STAT_REQUEST;
    request[1] = path;
    return service.call(request).then((response) {
      if (_isErrorResponse(response)) {
        throw _exceptionFromResponse(response,
                                     "Error getting stat",
                                     path);
      }
      // Unwrap the real list from the "I'm not an error" wrapper.
      List data = response[1];
      return new FileStat._internal(
          new DateTime.fromMillisecondsSinceEpoch(data[_CHANGED_TIME] * 1000),
          new DateTime.fromMillisecondsSinceEpoch(data[_MODIFIED_TIME] * 1000),
          new DateTime.fromMillisecondsSinceEpoch(data[_ACCESSED_TIME] * 1000),
          FileSystemEntityType._lookup(data[_TYPE]),
          data[_MODE],
          data[_SIZE]);
    });
  }

  String toString() => """
FileStat: type $type
          changed $changed
          modified $modified
          accessed $accessed
          mode ${modeString()}
          size $size""";

  /**
   * Returns the mode value as a human-readable string, in the format
   * "rwxrwxrwx", reflecting the user, group, and world permissions to
   * read, write, and execute the file system object, with "-" replacing the
   * letter for missing permissions.  Extra permission bits may be represented
   * by prepending "(suid)", "(guid)", and/or "(sticky)" to the mode string.
   */
  String modeString() {
    var permissions = mode & 0xFFF;
    var codes = const ['---', '--x', '-w-', '-wx', 'r--', 'r-x', 'rw-', 'rwx'];
    var result = [];
    if ((permissions & 0x800) != 0) result.add("(suid) ");
    if ((permissions & 0x400) != 0) result.add("(guid) ");
    if ((permissions & 0x200) != 0) result.add("(sticky) ");
    result.add(codes[(permissions >> 6) & 0x7]);
    result.add(codes[(permissions >> 3) & 0x7]);
    result.add(codes[permissions & 0x7]);
    return result.join();
  }

  /**
   * The time of the last change to the data or metadata of the file system
   * object.  On Windows platforms, this is instead the file creation time.
   */
  final DateTime changed;
  /**
   * The time of the last change to the data of the file system
   * object.
   */
  final DateTime modified;
  /**
   * The time of the last access to the data of the file system
   * object.  On Windows platforms, this may have 1 day granularity, and be
   * out of date by an hour.
   */
  final DateTime accessed;
  /**
   * The type of the object (file, directory, or link).  If the call to
   * stat() fails, the type of the returned object is NOT_FOUND.
   */
  final FileSystemEntityType type;
  /**
   * The mode of the file system object.  Permissions are encoded in the lower
   * 16 bits of this number, and can be decoded using the [modeString] getter.
   */
  final int mode;
  /**
   * The size of the file system object.
   */
  final int size;
}


/**
 * A [FileSystemEntity] is a common super class for [File] and
 * [Directory] objects.
 *
 * [FileSystemEntity] objects are returned from directory listing
 * operations. To determine if a FileSystemEntity is a [File] or a
 * [Directory], perform a type check:
 *
 *     if (entity is File) (entity as File).readAsStringSync();
 */
abstract class FileSystemEntity {
  String get path;

  external static _getType(String path, bool followLinks);
  external static _identical(String path1, String path2);

  static int _getTypeSync(String path, bool followLinks) {
    var result = _getType(path, followLinks);
    _throwIfError(result, 'Error getting type of FileSystemEntity');
    return result;
  }

  static Future<int> _getTypeAsync(String path, bool followLinks) {
    // Get a new file service port for each request.  We could also cache one.
    var service = _FileUtils._newServicePort();
    List request = new List(3);
    request[0] = _TYPE_REQUEST;
    request[1] = path;
    request[2] = followLinks;
    return service.call(request).then((response) {
      if (_isErrorResponse(response)) {
        throw _exceptionFromResponse(response, "Error getting type", path);
      }
      return response;
    });
  }

  /**
   * Synchronously checks whether two paths refer to the same object in the
   * file system. Returns a [:Future<bool>:] that completes with the result.
   *
   * Comparing a link to its target returns false, as does comparing two links
   * that point to the same target.  To check the target of a link, use
   * Link.target explicitly to fetch it.  Directory links appearing
   * inside a path are followed, though, to find the file system object.
   *
   * Completes the returned Future with an error if one of the paths points
   * to an object that does not exist.
   */
  static Future<bool> identical(String path1, String path2) {
    // Get a new file service port for each request.  We could also cache one.
    var service = _FileUtils._newServicePort();
    List request = new List(3);
    request[0] = _IDENTICAL_REQUEST;
    request[1] = path1;
    request[2] = path2;
    return service.call(request).then((response) {
      if (_isErrorResponse(response)) {
        throw _exceptionFromResponse(response,
            "Error in FileSystemEntity.identical($path1, $path2)", "");
      }
      return response;
    });
  }


  /**
   * Synchronously checks whether two paths refer to the same object in the
   * file system.
   *
   * Comparing a link to its target returns false, as does comparing two links
   * that point to the same target.  To check the target of a link, use
   * Link.target explicitly to fetch it.  Directory links appearing
   * inside a path are followed, though, to find the file system object.
   *
   * Throws an error if one of the paths points to an object that does not
   * exist.
   */
  static bool identicalSync(String path1, String path2) {
    var result = _identical(path1, path2);
    _throwIfError(result, 'Error in FileSystemEntity.identicalSync');
    return result;
  }

  /**
   * Checks whether the file system entity with this path exists. Returns
   * a [:Future<bool>:] that completes with the result.
   *
   * Since FileSystemEntity is abstract, every FileSystemEntity object
   * is actually an instance of one of the subclasses [File],
   * [Directory], and [Link].  Calling [exists] on an instance of one
   * of these subclasses checks whether the object exists in the file
   * system object exists and is of the correct type (file, directory,
   * or link).  To check whether a path points to an object on the
   * file system, regardless of the object's type, use the [type]
   * static method.
   *
   */
  Future<bool> exists();

  /**
   * Synchronously checks whether the file system entity with this path
   * exists.
   *
   * Since FileSystemEntity is abstract, every FileSystemEntity object
   * is actually an instance of one of the subclasses [File],
   * [Directory], and [Link].  Calling [existsSync] on an instance of
   * one of these subclasses checks whether the object exists in the
   * file system object exists and is of the correct type (file,
   * directory, or link).  To check whether a path points to an object
   * on the file system, regardless of the object's type, use the
   * [typeSync] static method.
   */
  bool existsSync();

  /**
   * Renames this file system entity. Returns a `Future<FileSystemEntity>`
   * that completes with a [FileSystemEntity] instance for the renamed
   * file system entity.
   *
   * If [newPath] identifies an existing entity of the same type, that entity
   * is replaced. If [newPath] identifies an existing entity of a different
   * type, the operation fails and the future completes with an exception.
   */
  Future<FileSystemEntity> rename(String newPath);

   /**
   * Synchronously renames this file system entity. Returns a [FileSystemEntity]
   * instance for the renamed entity.
   *
   * If [newPath] identifies an existing entity of the same type, that entity
   * is replaced. If [newPath] identifies an existing entity of a different
   * type, the operation fails and an exception is thrown.
   */
  FileSystemEntity renameSync(String newPath);

  /**
   * Calls the operating system's stat() function on the [path] of this
   * [FileSystemEntity].  Identical to [:FileStat.stat(this.path):].
   *
   * Returns a [:Future<FileStat>:] object containing the data returned by
   * stat().
   *
   * If the call fails, completes the future with a [FileStat] object
   * with .type set to
   * FileSystemEntityType.NOT_FOUND and the other fields invalid.
   */
  Future<FileStat> stat();

  /**
   * Synchronously calls the operating system's stat() function on the
   * [path] of this [FileSystemEntity].
   * Identical to [:FileStat.statSync(this.path):].
   *
   * Returns a [FileStat] object containing the data returned by stat().
   *
   * If the call fails, returns a [FileStat] object with .type set to
   * FileSystemEntityType.NOT_FOUND and the other fields invalid.
   */
  FileStat statSync();


  /**
   * Finds the type of file system object that a path points to. Returns
   * a [:Future<FileSystemEntityType>:] that completes with the result.
   *
   * [FileSystemEntityType] has the constant instances FILE, DIRECTORY,
   * LINK, and NOT_FOUND.  [type] will return LINK only if the optional
   * named argument [followLinks] is false, and [path] points to a link.
   * If the path does not point to a file system object, or any other error
   * occurs in looking up the path, NOT_FOUND is returned.  The only
   * error or exception that may be put on the returned future is ArgumentError,
   * caused by passing the wrong type of arguments to the function.
   */
  static Future<FileSystemEntityType> type(String path,
                                           {bool followLinks: true})
      => _getTypeAsync(path, followLinks).then(FileSystemEntityType._lookup);

  /**
   * Synchronously finds the type of file system object that a path points to.
   * Returns a [FileSystemEntityType].
   *
   * [FileSystemEntityType] has the constant instances FILE, DIRECTORY,
   * LINK, and NOT_FOUND.  [type] will return LINK only if the optional
   * named argument [followLinks] is false, and [path] points to a link.
   * If the path does not point to a file system object, or any other error
   * occurs in looking up the path, NOT_FOUND is returned.  The only
   * error or exception that may be thrown is ArgumentError,
   * caused by passing the wrong type of arguments to the function.
   */
  static FileSystemEntityType typeSync(String path, {bool followLinks: true})
      => FileSystemEntityType._lookup(_getTypeSync(path, followLinks));


  /**
   * Checks if type(path, followLinks: false) returns
   * FileSystemEntityType.LINK.
   */
  static Future<bool> isLink(String path) => _getTypeAsync(path, false)
      .then((type) => (type == FileSystemEntityType.LINK._type));

  /**
   * Checks if type(path) returns FileSystemEntityType.FILE.
   */
  static Future<bool> isFile(String path) => _getTypeAsync(path, true)
      .then((type) => (type == FileSystemEntityType.FILE._type));

  /**
   * Checks if type(path) returns FileSystemEntityType.DIRECTORY.
   */
  static Future<bool> isDirectory(String path) => _getTypeAsync(path, true)
      .then((type) => (type == FileSystemEntityType.DIRECTORY._type));

  /**
   * Synchronously checks if typeSync(path, followLinks: false) returns
   * FileSystemEntityType.LINK.
   */
  static bool isLinkSync(String path) =>
      (_getTypeSync(path, false) == FileSystemEntityType.LINK._type);

  /**
   * Synchronously checks if typeSync(path) returns
   * FileSystemEntityType.FILE.
   */
  static bool isFileSync(String path) =>
      (_getTypeSync(path, true) == FileSystemEntityType.FILE._type);

  /**
   * Synchronously checks if typeSync(path) returns
   * FileSystemEntityType.DIRECTORY.
   */
  static bool isDirectorySync(String path) =>
      (_getTypeSync(path, true) == FileSystemEntityType.DIRECTORY._type);


  static _throwIfError(Object result, String msg) {
    if (result is OSError) {
      throw new FileException(msg, result);
    } else if (result is ArgumentError) {
      throw result;
    }
  }
}
