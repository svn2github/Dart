library EventsTest;
import '../../pkg/unittest/lib/unittest.dart';
import '../../pkg/unittest/lib/html_config.dart';
import 'dart:html';

main() {
  useHtmlConfiguration();
  test('TimeStamp', () {
    Event event = new Event('test');

    int timeStamp = event.timeStamp;
    expect(timeStamp, greaterThan(0));
  });
  // The next test is not asynchronous because [on['test'].dispatch(event)] fires the event
  // and event listener synchronously.
  test('EventTarget', () {
    Element element = new Element.tag('test');
    element.id = 'eventtarget';
    window.document.body.nodes.add(element);

    int invocationCounter = 0;
    void handler(Event e) {
      expect(e.type, equals('test'));
      Element target = e.target;
      expect(element, equals(target));
      invocationCounter++;
    }

    Event event = new Event('test');

    invocationCounter = 0;
    element.on['test'].dispatch(event);
    expect(invocationCounter, isZero);

    element.on['test'].add(handler, false);
    invocationCounter = 0;
    element.on['test'].dispatch(event);
    expect(invocationCounter, 1);

    element.on['test'].remove(handler, false);
    invocationCounter = 0;
    element.on['test'].dispatch(event);
    expect(invocationCounter, isZero);

    element.on['test'].add(handler, false);
    invocationCounter = 0;
    element.on['test'].dispatch(event);
    expect(invocationCounter, 1);

    element.on['test'].add(handler, false);
    invocationCounter = 0;
    element.on['test'].dispatch(event);
    expect(invocationCounter, 1);
  });
  test('InitMouseEvent', () {
    DivElement div = new Element.tag('div');
    MouseEvent event = new MouseEvent('zebra', relatedTarget: div);
  });
}
