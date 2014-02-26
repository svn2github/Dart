import 'package:angular/angular.dart';
import 'dart:html';
import 'dart:math';
import 'dart:core';

var random = new Random();
var width = 400;
var height = 400;
var speed = .05;

class BallModel {
  var x = width * random.nextDouble();
  var y = height * random.nextDouble();
  var velX = 2 * speed * random.nextDouble() - speed;
  var velY = 2 * speed * random.nextDouble() - speed;
  var color = BallModel._color();

  static _color() {
    var color = '#';
    for(var i=0; i < 6; i++) {
      color += (16 * random.nextDouble()).floor().toRadixString(16);
    }
    return color;
  }

}

@NgController(
  selector: '[bounce-controller]',
  publishAs: 'bounce'
)
class BounceController {
  var lastTime = window.performance.now();
  var run = true;
  var fps = 0;
  var digestTime = 0;
  var balls = [];
  var zone;
  var scope;
  var ballClassName = 'ball';

  BounceController(NgZone this.zone, Scope this.scope) {
    changeCount(100);
    tick();
  }

  toggleCSS() {
    ballClassName = ballClassName == '' ? 'ball' : '';
  }

  playPause() {
    run = !run;
    if (run) requestAnimationFrame(tick);
  }

  requestAnimationFrame(fn) {
    window.requestAnimationFrame((_) => zone.run(fn));
  }

  changeCount(count) {
    while(count > 0) {
      balls.add(new BallModel());
      count--;
    }
    while(count < 0) {
      balls.removeAt(0);
      count++;
    }
    tick();
  }

  timeDigest() {
    var start = window.performance.now();
    scope.$evalAsync(() {
      digestTime = (window.performance.now() - start).round();
    }, outsideDigest: true);
  }

  tick() {
    var now = window.performance.now(),
        delay = now - lastTime;

    fps = (1000/delay).round();
    for(var i=0, ii=balls.length; i<ii; i++) {
      var b = balls[i];
      b.x += delay * b.velX;
      b.y += delay * b.velY;
      if (b.x < 0) { b.x *= -1; b.velX *= -1; }
      if (b.y < 0) { b.y *= -1; b.velY *= -1; }
      if (b.x > width) { b.x = 2*width - b.x; b.velX *= -1; }
      if (b.y > height) { b.y = 2*height - b.y; b.velY *= -1; }
    }
    lastTime = now;
    timeDigest();
    if (run) requestAnimationFrame(tick);
  }
}

@NgDirective(
  selector: '[ball-position]',
  map: const {
    "ballPosition": '=>position'
  }
)
class BallPositionDirective {
  Element element;
  Scope scope;
  BallPositionDirective(Element this.element, Scope this.scope);

  set position(BallModel model) {
    element.style.backgroundColor = model.color;
    scope.$watch(() {
      element.style.left = '${model.x + 10}px';
      element.style.top = '${model.y + 10}px';
    });
  }
}

class MyModule extends Module {
  MyModule() {
    type(BounceController);
    type(BallPositionDirective);
  }
}

main() {
  ngBootstrap(module: new MyModule());
}
