library IndexedDB3Test;
import '../../pkg/unittest/lib/unittest.dart';
import '../../pkg/unittest/lib/html_config.dart';
import 'dart:html';
import 'dart:indexed_db';

// Read with cursor.

const String DB_NAME = 'Test';
const String STORE_NAME = 'TEST';
const int VERSION = 1;

class Test {
  fail(message) => (e) {
    guardAsync(() {
      expect(false, isTrue, reason: 'IndexedDB failure: $message');
    });
  };

  _createObjectStore(db) {
    try {
      // Nuke object store if it already exists.
      db.deleteObjectStore(STORE_NAME);
    }
    on DomException catch(e) { } // Chrome and Firefox
    db.createObjectStore(STORE_NAME);
  }

  var db;

  _openDb(afterOpen()) {
    var request = window.indexedDB.open(DB_NAME, VERSION);
    if (request is OpenDBRequest) {
      // New upgrade protocol. FireFox 15, Chrome 24, hopefully IE10.
      request.on.success.add(expectAsync1((e) {
            db = e.target.result;
            afterOpen();
          }));
      request.on.upgradeNeeded.add((e) {
          guardAsync(() {
              _createObjectStore(e.target.result);
            });
        });
      request.on.error.add(fail('open'));
    } else {
      // Legacy setVersion upgrade protocol. Chrome < 23.
      request.on.success.add(expectAsync1((e) {
            db = e.target.result;
            if (db.version != '$VERSION') {
              var setRequest = db.setVersion('$VERSION');
              setRequest.on.success.add(
                  expectAsync1((e) {
                      _createObjectStore(db);
                      var transaction = e.target.result;
                      transaction.on.complete.add(
                          expectAsync1((e) => afterOpen()));
                      transaction.on.error.add(fail('Upgrade'));
                    }));
              setRequest.on.error.add(fail('setVersion error'));
            } else {
              afterOpen();
            }
          }));
      request.on.error.add(fail('open'));
    }
  }

  _createAndOpenDb(afterOpen()) {
    var request = window.indexedDB.deleteDatabase(DB_NAME);
    request.on.success.add(expectAsync1((e) { _openDb(afterOpen); }));
    request.on.error.add(fail('delete old Db'));
  }

  writeItems(int index) {
    if (index < 100) {
      var transaction = db.transaction([STORE_NAME], 'readwrite');
      var request = transaction.objectStore(STORE_NAME)
          .put('Item $index', index);
      request.on.success.add(expectAsync1((e) {
          writeItems(index + 1);
        }
      ));
      request.on.error.add(fail('put'));
    }
  }

  setupDb() { _createAndOpenDb(() => writeItems(0)); }

  readAllViaCursor() {
    Transaction txn = db.transaction(STORE_NAME, 'readonly');
    ObjectStore objectStore = txn.objectStore(STORE_NAME);
    Request cursorRequest = objectStore.openCursor();
    int itemCount = 0;
    int sumKeys = 0;
    int lastKey = null;
    cursorRequest.on.success.add(expectAsync1((e) {
      var cursor = e.target.result;
      if (cursor != null) {
        lastKey = cursor.key;
        itemCount += 1;
        sumKeys += cursor.key;
        window.console.log('${cursor.key}  ${cursor.value}');
        expect(cursor.value, 'Item ${cursor.key}');
        cursor.continueFunction();
      } else {
        // Done
        expect(lastKey, 99);
        expect(itemCount, 100);
        expect(sumKeys, (100 * 99) ~/ 2);
      }
    }, count:101));
    cursorRequest.on.error.add(fail('openCursor'));
  }

  readAllReversedViaCursor() {
    Transaction txn = db.transaction(STORE_NAME, 'readonly');
    ObjectStore objectStore = txn.objectStore(STORE_NAME);
    // TODO: create a KeyRange(0,100)
    Request cursorRequest = objectStore.openCursor(null, 'prev');
    int itemCount = 0;
    int sumKeys = 0;
    int lastKey = null;
    cursorRequest.on.success.add(expectAsync1((e) {
      var cursor = e.target.result;
      if (cursor != null) {
        lastKey = cursor.key;
        itemCount += 1;
        sumKeys += cursor.key;
        expect(cursor.value, 'Item ${cursor.key}');
        cursor.continueFunction();
      } else {
        // Done
        expect(lastKey, 0);  // i.e. first key (scanned in reverse).
        expect(itemCount, 100);
        expect(sumKeys, (100 * 99) ~/ 2);
      }
    }, count:101));
    cursorRequest.on.error.add(fail('openCursor'));
  }
}

main() {
  useHtmlConfiguration();

  var test_ = new Test();
  test('prepare', test_.setupDb);
  test('readAll1', test_.readAllViaCursor);
  test('readAll2', test_.readAllReversedViaCursor);
}
