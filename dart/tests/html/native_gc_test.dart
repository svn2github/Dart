library NativeGCTest;
import '../../pkg/unittest/lib/unittest.dart';
import '../../pkg/unittest/lib/html_config.dart';
import 'dart:html';

main() {
  useHtmlConfiguration();

  test('EventListener', () {
      final int N = 1000000;
      final int M = 1000;

      var div;
      for (int i = 0; i < M; ++i) {
        // This memory should be freed when the listener below is
        // collected.
        List l = new List.fixedLength(N);

        // Record the iteration number.
        l[N - 1] = i;

        div = new Element.tag('div');
        div.on['test'].add((_) {
            // Only the final iteration's listener should be invoked.
            // Note: the reference to l keeps the entire list alive.
            expect(l[N - 1], M - 1);
          }, false);
      }

      final event = new Event('test');
      div.on['test'].dispatch(event);
  });

  test('WindowEventListener', () {
    String message = 'WindowEventListenerTestPingMessage';

    Element testDiv = new DivElement();
    testDiv.id = '#TestDiv';
    document.body.nodes.add(testDiv);
    window.on.message.add((e) {
      if (e.data == message) testDiv.click();
    });

    for (int i = 0; i < 100; ++i) {
      triggerMajorGC();
    }

    testDiv.on.click.add(expectAsync1((e) {}));
    window.postMessage(message, '*');
  });
}

void triggerMajorGC() {
  List list = new List.fixedLength(1000000);
  Element div = new DivElement();
  div.on.click.add((e) => print(list[0]));
}
