// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

part of dart.async;

/** Throws the given error in the next cycle. */
_throwDelayed(var error, [Object stackTrace]) {
  // We are going to reach the top-level here, but there might be a global
  // exception handler. This means that we shouldn't print the stack trace.
  // TODO(floitsch): Find better solution that doesn't print the stack trace
  // if there is a global exception handler.
  runAsync(() {
    if (stackTrace != null) print(stackTrace);
    var trace = getAttachedStackTrace(error);
    if (trace != null && trace != stackTrace) print(trace);
    throw error;
  });
}

/** Abstract and private interface for a place to put events. */
abstract class _EventSink<T> {
  void _add(T data);
  void _addError(Object error);
  void _close();
}

/**
 * Abstract and private interface for a place to send events.
 *
 * Used by event buffering to finally dispatch the pending event, where
 * [_EventSink] is where the event first enters the stream subscription,
 * and may yet be buffered.
 */
abstract class _EventDispatch<T> {
  void _sendData(T data);
  void _sendError(Object error);
  void _sendDone();
}

/**
 * Default implementation of stream subscription of buffering events.
 *
 * The only public methods are those of [StreamSubscription], so instances of
 * [_BufferingStreamSubscription] can be returned directly as a
 * [StreamSubscription] without exposing internal functionality.
 *
 * The [StreamController] is a public facing version of [Stream] and this class,
 * with some methods made public.
 *
 * The user interface of [_BufferingStreamSubscription] are the following
 * methods:
 * * [_add]: Add a data event to the stream.
 * * [_addError]: Add an error event to the stream.
 * * [_close]: Request to close the stream.
 * * [_onCancel]: Called when the subscription will provide no more events,
 *     either due to being actively canceled, or after sending a done event.
 * * [_onPause]: Called when the subscription wants the event source to pause.
 * * [_onResume]: Called when allowing new events after a pause.
 * The user should not add new events when the subscription requests a paused,
 * but if it happens anyway, the subscription will enqueue the events just as
 * when new events arrive while still firing an old event.
 */
class _BufferingStreamSubscription<T> implements StreamSubscription<T>,
                                                 _EventSink<T>,
                                                 _EventDispatch<T> {
  /** The `cancelOnError` flag from the `listen` call. */
  static const int _STATE_CANCEL_ON_ERROR = 1;
  /**
   * Whether the "done" event has been received.
   * No further events are accepted after this.
   */
  static const int _STATE_CLOSED = 2;
  /**
   * Set if the input has been asked not to send events.
   *
   * This is not the same as being paused, since the input will remain paused
   * after a call to [resume] if there are pending events.
   */
  static const int _STATE_INPUT_PAUSED = 4;
  /**
   * Whether the subscription has been canceled.
   *
   * Set by calling [cancel], or by handling a "done" event, or an "error" event
   * when `cancelOnError` is true.
   */
  static const int _STATE_CANCELED = 8;
  static const int _STATE_IN_CALLBACK = 16;
  static const int _STATE_HAS_PENDING = 32;
  static const int _STATE_PAUSE_COUNT = 64;
  static const int _STATE_PAUSE_COUNT_SHIFT = 6;

  /* Event handlers provided in constructor. */
  /* TODO(7733): Fix Function->_DataHandler<T> when dart2js understands
   * parameterized function types. */
  Function _onData;
  _ErrorHandler _onError;
  _DoneHandler _onDone;

  /** Bit vector based on state-constants above. */
  int _state;

  /**
   * Queue of pending events.
   *
   * Is created when necessary, or set in constructor for preconfigured events.
   */
  _PendingEvents _pending;

  _BufferingStreamSubscription(this._onData,
                               this._onError,
                               this._onDone,
                               bool cancelOnError)
      : _state = (cancelOnError ? _STATE_CANCEL_ON_ERROR : 0) {
    assert(_onData != null);
    assert(_onError != null);
    assert(_onDone != null);
  }

  /**
   * Sets the subscription's pending events object.
   *
   * This can only be done once. The pending events object is used for the
   * rest of the subscription's life cycle.
   */
  void _setPendingEvents(_PendingEvents pendingEvents) {
    assert(_pending == null);
    if (pendingEvents == null) return;
    _pending = pendingEvents;
    if (!pendingEvents.isEmpty) {
      _state |= _STATE_HAS_PENDING;
      _pending.schedule(this);
    }
  }

  /**
   * Extracts the pending events from a canceled stream.
   *
   * This can only be done during the [_onCancel] method call. After that,
   * any remaining pending events will be cleared.
   */
  _PendingEvents _extractPending() {
    assert(_isCanceled);
    _PendingEvents events = _pending;
    _pending = null;
    return events;
  }

  // StreamSubscription interface.

  void onData(void handleData(T event)) {
    if (handleData == null) handleData = _nullDataHandler;
    _onData = handleData;
  }

  void onError(void handleError(error)) {
    if (handleError == null) handleError = _nullErrorHandler;
    _onError = handleError;
  }

  void onDone(void handleDone()) {
    if (handleDone == null) handleDone = _nullDoneHandler;
    _onDone = handleDone;
  }

  void pause([Future resumeSignal]) {
    if (_isCanceled) return;
    bool wasPaused = _isPaused;
    bool wasInputPaused = _isInputPaused;
    // Increment pause count and mark input paused (if it isn't already).
    _state = (_state + _STATE_PAUSE_COUNT) | _STATE_INPUT_PAUSED;
    if (resumeSignal != null) resumeSignal.whenComplete(resume);
    if (!wasPaused && _pending != null) _pending.cancelSchedule();
    if (!wasInputPaused && !_inCallback) _guardCallback(_onPause);
  }

  void resume() {
    if (_isCanceled) return;
    if (_isPaused) {
      _decrementPauseCount();
      if (!_isPaused) {
        if (_hasPending && !_pending.isEmpty) {
          // Input is still paused.
          _pending.schedule(this);
        } else {
          assert(_mayResumeInput);
          _state &= ~_STATE_INPUT_PAUSED;
          if (!_inCallback) _guardCallback(_onResume);
        }
      }
    }
  }

  void cancel() {
    if (_isCanceled) return;
    _cancel();
    if (!_inCallback) {
      // otherwise checkState will be called after firing or callback completes.
      _state |= _STATE_IN_CALLBACK;
      _onCancel();
      _pending = null;
      _state &= ~_STATE_IN_CALLBACK;
    }
  }

  Future asFuture([var futureValue]) {
    _FutureImpl<T> result = new _FutureImpl<T>();

    // Overwrite the onDone and onError handlers.
    _onDone = () { result._setValue(futureValue); };
    _onError = (error) {
      cancel();
      result._setError(error);
    };

    return result;
  }

  // State management.

  bool get _isInputPaused => (_state & _STATE_INPUT_PAUSED) != 0;
  bool get _isClosed => (_state & _STATE_CLOSED) != 0;
  bool get _isCanceled => (_state & _STATE_CANCELED) != 0;
  bool get _inCallback => (_state & _STATE_IN_CALLBACK) != 0;
  bool get _hasPending => (_state & _STATE_HAS_PENDING) != 0;
  bool get _isPaused => _state >= _STATE_PAUSE_COUNT;
  bool get _canFire => _state < _STATE_IN_CALLBACK;
  bool get _mayResumeInput =>
      !_isPaused && (_pending == null || _pending.isEmpty);
  bool get _cancelOnError => (_state & _STATE_CANCEL_ON_ERROR) != 0;

  bool get isPaused => _isPaused;

  void _cancel() {
    _state |= _STATE_CANCELED;
    if (_hasPending) {
      _pending.cancelSchedule();
    }
  }

  /**
   * Increment the pause count.
   *
   * Also marks input as paused.
   */
  void _incrementPauseCount() {
    _state = (_state + _STATE_PAUSE_COUNT) | _STATE_INPUT_PAUSED;
  }

  /**
   * Decrements the pause count.
   *
   * Does not automatically unpause the input (call [_onResume]) when
   * the pause count reaches zero. This is handled elsewhere, and only
   * if there are no pending events buffered.
   */
  void _decrementPauseCount() {
    assert(_isPaused);
    _state -= _STATE_PAUSE_COUNT;
  }

  // _EventSink interface.

  void _add(T data) {
    assert(!_isClosed);
    if (_isCanceled) return;
    if (_canFire) {
      _sendData(data);
    } else {
      _addPending(new _DelayedData(data));
    }
  }

  void _addError(Object error) {
    if (_isCanceled) return;
    if (_canFire) {
      _sendError(error);  // Reports cancel after sending.
    } else {
      _addPending(new _DelayedError(error));
    }
  }

  void _close() {
    assert(!_isClosed);
    if (_isCanceled) return;
    _state |= _STATE_CLOSED;
    if (_canFire) {
      _sendDone();
    } else {
      _addPending(const _DelayedDone());
    }
  }

  // Hooks called when the input is paused, unpaused or canceled.
  // These must not throw. If overwritten to call user code, include suitable
  // try/catch wrapping and send any errors to [_throwDelayed].
  void _onPause() {
    assert(_isInputPaused);
  }

  void _onResume() {
    assert(!_isInputPaused);
  }

  void _onCancel() {
    assert(_isCanceled);
  }

  // Handle pending events.

  /**
   * Add a pending event.
   *
   * If the subscription is not paused, this also schedules a firing
   * of pending events later (if necessary).
   */
  void _addPending(_DelayedEvent event) {
    _StreamImplEvents pending = _pending;
    if (_pending == null) pending = _pending = new _StreamImplEvents();
    pending.add(event);
    if (!_hasPending) {
      _state |= _STATE_HAS_PENDING;
      if (!_isPaused) {
        _pending.schedule(this);
      }
    }
  }

  /* _EventDispatch interface. */

  void _sendData(T data) {
    assert(!_isCanceled);
    assert(!_isPaused);
    assert(!_inCallback);
    bool wasInputPaused = _isInputPaused;
    _state |= _STATE_IN_CALLBACK;
    try {
      _onData(data);
    } catch (e, s) {
      _throwDelayed(e, s);
    }
    _state &= ~_STATE_IN_CALLBACK;
    _checkState(wasInputPaused);
  }

  void _sendError(var error) {
    assert(!_isCanceled);
    assert(!_isPaused);
    assert(!_inCallback);
    bool wasInputPaused = _isInputPaused;
    _state |= _STATE_IN_CALLBACK;
    try {
      _onError(error);
    } catch (e, s) {
      _throwDelayed(e, s);
    }
    _state &= ~_STATE_IN_CALLBACK;
    if (_cancelOnError) {
      _cancel();
    }
    _checkState(wasInputPaused);
  }

  void _sendDone() {
    assert(!_isCanceled);
    assert(!_isPaused);
    assert(!_inCallback);
    _state |= (_STATE_CANCELED | _STATE_CLOSED | _STATE_IN_CALLBACK);
    try {
      _onDone();
    } catch (e, s) {
      _throwDelayed(e, s);
    }
    _onCancel();  // No checkState after cancel, it is always the last event.
    _state &= ~_STATE_IN_CALLBACK;
  }

  /**
   * Call a hook function.
   *
   * The call is properly wrapped in code to avoid other callbacks
   * during the call, and it checks for state changes after the call
   * that should cause further callbacks.
   */
  void _guardCallback(callback) {
    assert(!_inCallback);
    bool wasInputPaused = _isInputPaused;
    _state |= _STATE_IN_CALLBACK;
    callback();
    _state &= ~_STATE_IN_CALLBACK;
    _checkState(wasInputPaused);
  }

  /**
   * Check if the input needs to be informed of state changes.
   *
   * State changes are pausing, resuming and canceling.
   *
   * After canceling, no further callbacks will happen.
   *
   * The cancel callback is called after a user cancel, or after
   * the final done event is sent.
   */
  void _checkState(bool wasInputPaused) {
    assert(!_inCallback);
    if (_hasPending && _pending.isEmpty) {
      _state &= ~_STATE_HAS_PENDING;
      if (_isInputPaused && _mayResumeInput) {
        _state &= ~_STATE_INPUT_PAUSED;
      }
    }
    // If the state changes during a callback, we immediately
    // make a new state-change callback. Loop until the state didn't change.
    while (true) {
      if (_isCanceled) {
        _onCancel();
        _pending = null;
        return;
      }
      bool isInputPaused = _isInputPaused;
      if (wasInputPaused == isInputPaused) break;
      _state ^= _STATE_IN_CALLBACK;
      if (isInputPaused) {
        _onPause();
      } else {
        _onResume();
      }
      _state &= ~_STATE_IN_CALLBACK;
      wasInputPaused = isInputPaused;
    }
    if (_hasPending && !_isPaused) {
      _pending.schedule(this);
    }
  }
}

// -------------------------------------------------------------------
// Common base class for single and multi-subscription streams.
// -------------------------------------------------------------------
abstract class _StreamImpl<T> extends Stream<T> {
  // ------------------------------------------------------------------
  // Stream interface.

  StreamSubscription<T> listen(void onData(T data),
                               { void onError(error),
                                 void onDone(),
                                 bool cancelOnError }) {
    if (onData == null) onData = _nullDataHandler;
    if (onError == null) onError = _nullErrorHandler;
    if (onDone == null) onDone = _nullDoneHandler;
    cancelOnError = identical(true, cancelOnError);
    StreamSubscription subscription =
        _createSubscription(onData, onError, onDone, cancelOnError);
    _onListen(subscription);
    return subscription;
  }

  // -------------------------------------------------------------------
  /** Create a subscription object. Called by [subcribe]. */
  _BufferingStreamSubscription<T> _createSubscription(
      void onData(T data),
      void onError(error),
      void onDone(),
      bool cancelOnError) {
    return new _BufferingStreamSubscription<T>(
        onData, onError, onDone, cancelOnError);
  }

  /** Hook called when the subscription has been created. */
  void _onListen(StreamSubscription subscription) {}
}

typedef _PendingEvents _EventGenerator();

/** Stream that generates its own events. */
class _GeneratedStreamImpl<T> extends _StreamImpl<T> {
  final _EventGenerator _pending;
  /**
   * Initializes the stream to have only the events provided by a
   * [_PendingEvents].
   *
   * A new [_PendingEvents] must be generated for each listen.
   */
  _GeneratedStreamImpl(this._pending);

  StreamSubscription _createSubscription(void onData(T data),
                                         void onError(Object error),
                                         void onDone(),
                                         bool cancelOnError) {
    _BufferingStreamSubscription<T> subscription =
         new _BufferingStreamSubscription(
             onData, onError, onDone, cancelOnError);
    subscription._setPendingEvents(_pending());
    return subscription;
  }
}


/** Pending events object that gets its events from an [Iterable]. */
class _IterablePendingEvents<T> extends _PendingEvents {
  // The iterator providing data for data events.
  // Set to null when iteration has completed.
  Iterator<T> _iterator;

  _IterablePendingEvents(Iterable<T> data) : _iterator = data.iterator;

  bool get isEmpty => _iterator == null;

  void handleNext(_EventDispatch dispatch) {
    if (_iterator == null) {
      throw new StateError("No events pending.");
    }
    // Send one event per call to moveNext.
    // If moveNext returns true, send the current element as data.
    // If moveNext returns false, send a done event and clear the _iterator.
    // If moveNext throws an error, send an error and clear the _iterator.
    // After an error, no further events will be sent.
    bool isDone;
    try {
      isDone = !_iterator.moveNext();
    } catch (e, s) {
      _iterator = null;
      dispatch._sendError(_asyncError(e, s));
      return;
    }
    if (!isDone) {
      dispatch._sendData(_iterator.current);
    } else {
      _iterator = null;
      dispatch._sendDone();
    }
  }

  void clear() {
    if (isScheduled) cancelSchedule();
    _iterator = null;
  }
}


// Internal helpers.

// Types of the different handlers on a stream. Types used to type fields.
typedef void _DataHandler<T>(T value);
typedef void _ErrorHandler(error);
typedef void _DoneHandler();


/** Default data handler, does nothing. */
void _nullDataHandler(var value) {}

/** Default error handler, reports the error to the global handler. */
void _nullErrorHandler(error) {
  _throwDelayed(error);
}

/** Default done handler, does nothing. */
void _nullDoneHandler() {}


/** A delayed event on a buffering stream subscription. */
abstract class _DelayedEvent {
  /** Added as a linked list on the [StreamController]. */
  _DelayedEvent next;
  /** Execute the delayed event on the [StreamController]. */
  void perform(_EventDispatch dispatch);
}

/** A delayed data event. */
class _DelayedData<T> extends _DelayedEvent{
  final T value;
  _DelayedData(this.value);
  void perform(_EventDispatch<T> dispatch) {
    dispatch._sendData(value);
  }
}

/** A delayed error event. */
class _DelayedError extends _DelayedEvent {
  final error;
  _DelayedError(this.error);
  void perform(_EventDispatch dispatch) {
    dispatch._sendError(error);
  }
}

/** A delayed done event. */
class _DelayedDone implements _DelayedEvent {
  const _DelayedDone();
  void perform(_EventDispatch dispatch) {
    dispatch._sendDone();
  }

  _DelayedEvent get next => null;

  void set next(_DelayedEvent _) {
    throw new StateError("No events after a done.");
  }
}

/**
 * Simple internal doubly-linked list implementation.
 *
 * In an internal linked list, the links are in the data objects themselves,
 * instead of in a separate object. That means each element can be in at most
 * one list at a time.
 *
 * All links are always members of an element cycle. At creation it's a
 * singleton cycle.
 */
abstract class _InternalLink {
  _InternalLink _nextLink;
  _InternalLink _previousLink;

  _InternalLink() {
    this._previousLink = this._nextLink = this;
  }

  /* Removes a link from any list it may be part of, and links it to itself. */
  static void unlink(_InternalLink element) {
    _InternalLink next = element._nextLink;
    _InternalLink previous = element._previousLink;
    next._previousLink = previous;
    previous._nextLink = next;
    element._nextLink = element._previousLink = element;
  }

  /** Check whether an element is unattached to other elements. */
  static bool isUnlinked(_InternalLink element) {
    return identical(element, element._nextLink);
  }
}

/**
 * Marker interface for "list" links.
 *
 * An "InternalLinkList" is an abstraction on top of a link cycle, where the
 * "list" object itself is not considered an element (it's just a header link
 * created to avoid edge cases).
 * An element is considered part of a list if it is in the list's cycle.
 * There should never be more than one "list" object in a cycle.
 */
abstract class _InternalLinkList extends _InternalLink {
  /**
   * Adds an element to a list, just before the header link.
   *
   * This effectively adds it at the end of the list.
   */
  static void add(_InternalLinkList list, _InternalLink element) {
    if (!_InternalLink.isUnlinked(element)) _InternalLink.unlink(element);
    _InternalLink listEnd = list._previousLink;
    listEnd._nextLink = element;
    list._previousLink = element;
    element._previousLink = listEnd;
    element._nextLink = list;
  }

  /** Removes an element from its list. */
  static void remove(_InternalLink element) {
    _InternalLink.unlink(element);
  }

  /** Check whether a list contains no elements, only the header link. */
  static bool isEmpty(_InternalLinkList list) => _InternalLink.isUnlinked(list);

  /** Moves all elements from the list [other] to [list]. */
  static void addAll(_InternalLinkList list, _InternalLinkList other) {
    if (isEmpty(other)) return;
    _InternalLink listLast = list._previousLink;
    _InternalLink otherNext = other._nextLink;
    listLast._nextLink = otherNext;
    otherNext._previousLink = listLast;
    _InternalLink otherLast = other._previousLink;
    list._previousLink = otherLast;
    otherLast._nextLink = list;
    // Clean up [other].
    other._nextLink = other._previousLink = other;
  }
}

/** Superclass for provider of pending events. */
abstract class _PendingEvents {
  // No async event has been scheduled.
  static const int _STATE_UNSCHEDULED = 0;
  // An async event has been scheduled to run a function.
  static const int _STATE_SCHEDULED = 1;
  // An async event has been scheduled, but it will do nothing when it runs.
  // Async events can't be preempted.
  static const int _STATE_CANCELED = 3;

  /**
   * State of being scheduled.
   *
   * Set to [_STATE_SCHEDULED] when pending events are scheduled for
   * async dispatch. Since we can't cancel a [runAsync] call, if schduling
   * is "canceled", the _state is simply set to [_STATE_CANCELED] which will
   * make the async code do nothing except resetting [_state].
   *
   * If events are scheduled while the state is [_STATE_CANCELED], it is
   * merely switched back to [_STATE_SCHEDULED], but no new call to [runAsync]
   * is performed.
   */
  int _state = _STATE_UNSCHEDULED;

  bool get isEmpty;

  bool get isScheduled => _state == _STATE_SCHEDULED;
  bool get _eventScheduled => _state >= _STATE_SCHEDULED;

  /**
   * Schedule an event to run later.
   *
   * If called more than once, it should be called with the same dispatch as
   * argument each time. It may reuse an earlier argument in some cases.
   */
  void schedule(_EventDispatch dispatch) {
    if (isScheduled) return;
    assert(!isEmpty);
    if (_eventScheduled) {
      assert(_state == _STATE_CANCELED);
      _state = _STATE_SCHEDULED;
      return;
    }
    runAsync(() {
      int oldState = _state;
      _state = _STATE_UNSCHEDULED;
      if (oldState == _STATE_CANCELED) return;
      handleNext(dispatch);
    });
    _state = _STATE_SCHEDULED;
  }

  void cancelSchedule() {
    if (isScheduled) _state = _STATE_CANCELED;
  }

  void handleNext(_EventDispatch dispatch);

  /** Throw away any pending events and cancel scheduled events. */
  void clear();
}


/** Class holding pending events for a [_StreamImpl]. */
class _StreamImplEvents extends _PendingEvents {
  /// Single linked list of [_DelayedEvent] objects.
  _DelayedEvent firstPendingEvent = null;
  /// Last element in the list of pending events. New events are added after it.
  _DelayedEvent lastPendingEvent = null;

  bool get isEmpty => lastPendingEvent == null;

  void add(_DelayedEvent event) {
    if (lastPendingEvent == null) {
      firstPendingEvent = lastPendingEvent = event;
    } else {
      lastPendingEvent = lastPendingEvent.next = event;
    }
  }

  void handleNext(_EventDispatch dispatch) {
    assert(!isScheduled);
    _DelayedEvent event = firstPendingEvent;
    firstPendingEvent = event.next;
    if (firstPendingEvent == null) {
      lastPendingEvent = null;
    }
    event.perform(dispatch);
  }

  void clear() {
    if (isScheduled) cancelSchedule();
    firstPendingEvent = lastPendingEvent = null;
  }
}

class _MultiplexerLinkedList {
  _MultiplexerLinkedList _next;
  _MultiplexerLinkedList _previous;

  void _unlink() {
    _previous._next = _next;
    _next._previous = _previous;
    _next = _previous = this;
  }

  void _insertBefore(_MultiplexerLinkedList newNext) {
    _MultiplexerLinkedList newPrevious = newNext._previous;
    newPrevious._next = this;
    newNext._previous = _previous;
    _previous._next = newNext;
    _previous = newPrevious;
  }
}

/**
 * A subscription used by [_SingleStreamMultiplexer].
 *
 * The [_SingleStreamMultiplexer] is a [Stream] which allows multiple
 * listeners at a time. It is used to implement [Stream.asBroadcastStream].
 *
 * It is itself listening to another stream for events, and it forwards all
 * events to all of its simultanous listeners.
 *
 * The listeners are [_MultiplexerSubscription]s and are kept as a linked list.
 */
// TODO(lrn): Change "implements" to "with" when automatic mixin constructors
//            are implemented.
class _MultiplexerSubscription<T> extends _BufferingStreamSubscription<T>
                                  implements _MultiplexerLinkedList {
  static const int _STATE_NOT_LISTENING = 0;
  // Bit that alternates between event firings. If bit matches the one currently
  // firing, the subscription will not be notified.
  static const int _STATE_EVENT_ID_BIT = 1;
  // Whether the subscription is listening at all. This should be set while
  // it is part of the linked list of listeners of a multiplexer stream.
  static const int _STATE_LISTENING = 2;
  // State bit set while firing an event.
  static const int _STATE_IS_FIRING = 4;
  // Bit set if a subscription is canceled while it's firing (the
  // [_STATE_IS_FIRING] bit is set).
  // If the subscription is canceled while firing, it is not removed from the
  // linked list immediately (to avoid breaking iteration), but is instead
  // removed after it is done firing.
  static const int _STATE_REMOVE_AFTER_FIRING = 8;

  // Firing state.
  int _multiplexState;

  _SingleStreamMultiplexer _source;

  _MultiplexerSubscription(this._source,
                           void onData(T data),
                           void onError(Object error),
                           void onDone(),
                           bool cancelOnError,
                           int nextEventId)
      : _multiplexState = _STATE_LISTENING | nextEventId,
        super(onData, onError, onDone, cancelOnError) {
    _next = _previous = this;
  }

  // Mixin workaround.
  _MultiplexerLinkedList _next;
  _MultiplexerLinkedList _previous;

  void _unlink() {
    _previous._next = _next;
    _next._previous = _previous;
    _next = _previous = this;
  }

  void _insertBefore(_MultiplexerLinkedList newNext) {
    _MultiplexerLinkedList newPrevious = newNext._previous;
    newPrevious._next = this;
    newNext._previous = _previous;
    _previous._next = newNext;
    _previous = newPrevious;
  }
  // End mixin.

  bool get _isListening => _multiplexState >= _STATE_LISTENING;
  bool get _isFiring => _multiplexState >= _STATE_IS_FIRING;
  bool get _removeAfterFiring => _multiplexState >= _STATE_REMOVE_AFTER_FIRING;
  int get _eventId => _multiplexState & _STATE_EVENT_ID_BIT;

  void _setRemoveAfterFiring() {
    assert(_isFiring);
    _multiplexState |= _STATE_REMOVE_AFTER_FIRING;
  }

  void _startFiring() {
    assert(!_isFiring);
    _multiplexState |= _STATE_IS_FIRING;
  }

  /// Marks listener as no longer firing, and toggles its event id.
  void _endFiring() {
    assert(_isFiring);
    _multiplexState ^= (_STATE_IS_FIRING | _STATE_EVENT_ID_BIT);
  }

  void _setNotListening() {
    assert(_isListening);
    _multiplexState = _STATE_NOT_LISTENING;
  }

  void _onCancel() {
    assert(_isListening);
    _source._removeListener(this);
  }
}

/**
 * A stream that sends events from another stream to multiple listeners.
 *
 * This is used to implement [Stream.asBroadcastStream].
 *
 * This stream allows listening more than once.
 * When the first listener is added, it starts listening on its source
 * stream for events. All events from the source stream are sent to all
 * active listeners. The listeners handle their own buffering.
 * When the last listener cancels, the source stream subscription is also
 * canceled, and no further listening is possible.
 */
// TODO(lrn): change "implements" to "with" when the VM supports it.
class _SingleStreamMultiplexer<T> extends Stream<T>
                                  implements _MultiplexerLinkedList,
                                             _EventDispatch<T> {
  final Stream<T> _source;
  StreamSubscription<T> _subscription;
  // Alternates between zero and one for each event fired.
  // Listeners are initialized with the next event's id, and will
  // only be notified if they match the event being fired.
  // That way listeners added during event firing will not receive
  // the current event.
  int _eventId = 0;

  bool _isFiring = false;

  // Remember events added while firing.
  _StreamImplEvents _pending;

  _SingleStreamMultiplexer(this._source) {
    _next = _previous = this;
  }

  bool get _hasPending => _pending != null && !_pending.isEmpty;

  // Should be mixin.
  _MultiplexerLinkedList _next;
  _MultiplexerLinkedList _previous;

  void _unlink() {
    _previous._next = _next;
    _next._previous = _previous;
    _next = _previous = this;
  }

  void _insertBefore(_MultiplexerLinkedList newNext) {
    _MultiplexerLinkedList newPrevious = newNext._previous;
    newPrevious._next = this;
    newNext._previous = _previous;
    _previous._next = newNext;
    _previous = newPrevious;
  }
  // End of mixin.

  StreamSubscription<T> listen(void onData(T data),
                               { void onError(Object error),
                                 void onDone(),
                                 bool cancelOnError }) {
    if (onData == null) onData = _nullDataHandler;
    if (onError == null) onError = _nullErrorHandler;
    if (onDone == null) onDone = _nullDoneHandler;
    cancelOnError = identical(true, cancelOnError);
    _MultiplexerSubscription subscription =
        new _MultiplexerSubscription(this, onData, onError, onDone,
                                     cancelOnError, _eventId);
    if (_subscription == null) {
      _subscription = _source.listen(_add, onError: _addError, onDone: _close);
    }
    subscription._insertBefore(this);
    return subscription;
  }

  /** Called by [_MultiplexerSubscription.remove]. */
  void _removeListener(_MultiplexerSubscription listener) {
    if (listener._isFiring) {
      listener._setRemoveAfterFiring();
    } else {
      _unlinkListener(listener);
    }
  }

  /** Remove a listener and close the subscription after the last one. */
  void _unlinkListener(_MultiplexerSubscription listener) {
    listener._setNotListening();
    listener._unlink();
    if (identical(_next, this)) {
      // Last listener removed.
      _cancel();
    }
  }

  void _cancel() {
    StreamSubscription subscription = _subscription;
    _subscription = null;
    subscription.cancel();
    if (_pending != null) _pending.cancelSchedule();
  }

  void _forEachListener(void action(_MultiplexerSubscription listener)) {
    int eventId = _eventId;
    _eventId ^= 1;
    _isFiring = true;
    _MultiplexerLinkedList entry = _next;
    // Call each listener in order. A listener can be removed during any
    // other listener's event. During its own event it will only be marked
    // as "to be removed", and it will be handled after the event is done.
    while (!identical(entry, this)) {
      _MultiplexerSubscription listener = entry;
      if (listener._eventId == eventId) {
        listener._startFiring();
        action(listener);
        listener._endFiring(); // Also toggles the event id.
      }
      entry = listener._next;
      if (listener._removeAfterFiring) {
        _unlinkListener(listener);
      }
    }
    _isFiring = false;
  }

  void _add(T data) {
    if (_isFiring || _hasPending) {
      _StreamImplEvents pending = _pending;
      if (pending == null) pending = _pending = new _StreamImplEvents();
      pending.add(new _DelayedData(data));
    } else {
      _sendData(data);
    }
  }

  void _addError(Object error) {
    if (_isFiring || _hasPending) {
      _StreamImplEvents pending = _pending;
      if (pending == null) pending = _pending = new _StreamImplEvents();
      pending.add(new _DelayedError(error));
    } else {
      _sendError(error);
    }
  }

  void _close() {
    if (_isFiring || _hasPending) {
      _StreamImplEvents pending = _pending;
      if (pending == null) pending = _pending = new _StreamImplEvents();
      pending.add(const _DelayedDone());
    } else {
      _sendDone();
    }
  }

  void _sendData(T data) {
    _forEachListener((_MultiplexerSubscription listener) {
      listener._add(data);
    });
    if (_hasPending) {
      _pending.schedule(this);
    }
  }

  void _sendError(Object error) {
    _forEachListener((_MultiplexerSubscription listener) {
      listener._addError(error);
    });
    if (_hasPending) {
      _pending.schedule(this);
    }
  }

  void _sendDone() {
    _forEachListener((_MultiplexerSubscription listener) {
      listener._setRemoveAfterFiring();
      listener._close();
    });
  }
}


/**
 * Simple implementation of [StreamIterator].
 */
class _StreamIteratorImpl<T> implements StreamIterator<T> {
  // Internal state of the stream iterator.
  // At any time, it is in one of these states.
  // The interpretation of the [_futureOrPrefecth] field depends on the state.
  // In _STATE_MOVING, the _data field holds the most recently returned
  // future.
  // When in one of the _STATE_EXTRA_* states, the it may hold the
  // next data/error object, and the subscription is paused.

  /// The simple state where [_data] holds the data to return, and [moveNext]
  /// is allowed. The subscription is actively listening.
  static const int _STATE_FOUND = 0;
  /// State set after [moveNext] has returned false or an error,
  /// or after calling [cancel]. The subscription is always canceled.
  static const int _STATE_DONE = 1;
  /// State set after calling [moveNext], but before its returned future has
  /// completed. Calling [moveNext] again is not allowed in this state.
  /// The subscription is actively listening.
  static const int _STATE_MOVING = 2;
  /// States set when another event occurs while in _STATE_FOUND.
  /// This extra overflow event is cached until the next call to [moveNext],
  /// which will complete as if it received the event normally.
  /// The subscription is paused in these states, so we only ever get one
  /// event too many.
  static const int _STATE_EXTRA_DATA = 3;
  static const int _STATE_EXTRA_ERROR = 4;
  static const int _STATE_EXTRA_DONE = 5;

  /// Subscription being listened to.
  StreamSubscription _subscription;

  /// The current element represented by the most recent call to moveNext.
  ///
  /// Is null between the time moveNext is called and its future completes.
  T _current = null;

  /// The future returned by the most recent call to [moveNext].
  ///
  /// Also used to store the next value/error in case the stream provides an
  /// event before [moveNext] is called again. In that case, the stream will
  /// be paused to prevent further events.
  var _futureOrPrefetch = null;

  /// The current state.
  int _state = _STATE_FOUND;

  _StreamIteratorImpl(final Stream<T> stream) {
    _subscription = stream.listen(_onData,
                                  onError: _onError,
                                  onDone: _onDone,
                                  cancelOnError: true);
  }

  T get current => _current;

  Future<bool> moveNext() {
    if (_state == _STATE_DONE) {
      return new _FutureImpl<bool>.immediate(false);
    }
    if (_state == _STATE_MOVING) {
      throw new StateError("Already waiting for next.");
    }
    if (_state == _STATE_FOUND) {
      _state = _STATE_MOVING;
      _futureOrPrefetch = new _FutureImpl<bool>();
      return _futureOrPrefetch;
    } else {
      assert(_state >= _STATE_EXTRA_DATA);
      switch (_state) {
        case _STATE_EXTRA_DATA:
          _state = _STATE_FOUND;
          _current = _futureOrPrefetch;
          _futureOrPrefetch = null;
          _subscription.resume();
          return new FutureImpl<bool>.immediate(true);
        case _STATE_EXTRA_ERROR:
          Object prefetch = _futureOrPrefetch;
          _cancel();
          return new FutureImpl<bool>.error(prefetch);
        case _STATE_EXTRA_DONE:
          _cancel();
          return new FutureImpl<bool>.immediate(false);
      }
    }
  }

  /** Clears up the internal state when the iterator ends. */
  void _clear() {
    _subscription = null;
    _futureOrPrefetch = null;
    _current = null;
    _state = _STATE_DONE;
  }

  void cancel() {
    StreamSubscription subscription = _subscription;
    if (_state == _STATE_MOVING) {
      _FutureImpl<bool> hasNext = _futureOrPrefetch;
      _clear();
      hasNext._setValue(false);
    } else {
      _clear();
    }
    subscription.cancel();
  }

  void _onData(T data) {
    if (_state == _STATE_MOVING) {
      _current = data;
      _FutureImpl<bool> hasNext = _futureOrPrefetch;
      _futureOrPrefetch = null;
      _state = _STATE_FOUND;
      hasNext._setValue(true);
      return;
    }
    _subscription.pause();
    assert(_futureOrPrefetch == null);
    _futureOrPrefetch = data;
    _state = _STATE_EXTRA_DATA;
  }

  void _onError(Object error) {
    if (_state == _STATE_MOVING) {
      _FutureImpl<bool> hasNext = _futureOrPrefetch;
      // We have cancelOnError: true, so the subscription is canceled.
      _clear();
      hasNext._setError(error);
      return;
    }
    _subscription.pause();
    assert(_futureOrPrefetch == null);
    _futureOrPrefetch = error;
    _state = _STATE_EXTRA_ERROR;
  }

  void _onDone() {
     if (_state == _STATE_MOVING) {
      _FutureImpl<bool> hasNext = _futureOrPrefetch;
      _clear();
      hasNext._setValue(false);
      return;
    }
    _subscription.pause();
    _futureOrPrefetch = null;
    _state = _STATE_EXTRA_DONE;
  }
}
