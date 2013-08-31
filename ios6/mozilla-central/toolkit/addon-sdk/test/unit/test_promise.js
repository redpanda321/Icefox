/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

let tests = [];

// Utility function to observe an failures in a promise
// This function is useful if the promise itself is
// not returned.
let observe_failures = function observe_failures(promise) {
  promise.then(null, function onReject(reason) {
    test.do_throw("Observed failure in test " + test + ": " + reason);
  });
};

// Test that all observers are notified
tests.push(make_promise_test(
  function notification(test) {
    // The size of the test
    const SIZE = 10;
    const RESULT = "this is an arbitrary value";

    // Number of observers that yet need to be notified
    let expected = SIZE;

    // |true| once an observer has been notified
    let notified = [];

    // The promise observed
    let source = Promise.defer();
    let result = Promise.defer();

    let install_observer = function install_observer(i) {
      observe_failures(source.promise.then(
        function onSuccess(value) {
          do_check_true(!notified[i], "Ensuring that observer is notified at most once");
          notified[i] = true;

          do_check_eq(value, RESULT, "Ensuring that the observed value is correct");
          if (--expected == 0) {
            result.resolve();
          }
        }));
    };

    // Install a number of observers before resolving
    let i;
    for (i = 0; i < SIZE/2; ++i) {
      install_observer(i);
    }

    source.resolve(RESULT);

    // Install remaining observers
    for(;i < SIZE; ++i) {
      install_observer(i);
    }

    return result;
  }));

// Test that all observers are notified at most once, even if source
// is resolved/rejected several times
tests.push(make_promise_test(
  function notification_once(test) {
    // The size of the test
    const SIZE = 10;
    const RESULT = "this is an arbitrary value";

    // Number of observers that yet need to be notified
    let expected = SIZE;

    // |true| once an observer has been notified
    let notified = [];

    // The promise observed
    let observed = Promise.defer();
    let result = Promise.defer();

    let install_observer = function install_observer(i) {
      observe_failures(observed.promise.then(
        function onSuccess(value) {
          do_check_true(!notified[i], "Ensuring that observer is notified at most once");
          notified[i] = true;

          do_check_eq(value, RESULT, "Ensuring that the observed value is correct");
          if (--expected == 0) {
            result.resolve();
          }
        }));
    };

    // Install a number of observers before resolving
    let i;
    for (i = 0; i < SIZE/2; ++i) {
      install_observer(i);
    }

    observed.resolve(RESULT);

    // Install remaining observers
    for(;i < SIZE; ++i) {
      install_observer(i);
    }

    // Resolve some more
    for (i = 0; i < 10; ++i) {
      observed.resolve(RESULT);
      observed.reject();
    }

    return result;
  }));

// Test that throwing an exception from a onResolve listener
// does not prevent other observers from receiving the notification
// of success.
tests.push(
  make_promise_test(function exceptions_do_not_stop_notifications(test)  {
    let source = Promise.defer();

    let exception_thrown = false;
    let exception_content = new Error("Boom!");

    let observer_1 = source.promise.then(
      function onResolve() {
        exception_thrown = true;
        throw exception_content;
      });

    let observer_2 = source.promise.then(
      function onResolve() {
        do_check_true(exception_thrown, "Second observer called after first observer has thrown");
      }
    );

    let result = observer_1.then(
      function onResolve() {
        do_throw("observer_1 should not have resolved");
      },
      function onReject(reason) {
        do_check_true(reason == exception_content, "Obtained correct rejection");
      }
    );

    source.resolve();
    return result;
  }
));

// Test that, once a promise is resolved, further resolve/reject
// are ignored.
tests.push(
  make_promise_test(function subsequent_resolves_are_ignored(test) {
    let deferred = Promise.defer();
    deferred.resolve(1);
    deferred.resolve(2);
    deferred.reject(3);

    let result = deferred.promise.then(
      function onResolve(value) {
        do_check_eq(value, 1, "Resolution chose the first value");
      },
      function onReject(reason) {
        do_throw("Obtained a rejection while the promise was already resolved");
      }
    );

    return result;
  }));

// Test that, once a promise is rejected, further resolve/reject
// are ignored.
tests.push(
  make_promise_test(function subsequent_rejects_are_ignored(test) {
    let deferred = Promise.defer();
    deferred.reject(1);
    deferred.reject(2);
    deferred.resolve(3);

    let result = deferred.promise.then(
      function onResolve() {
        do_throw("Obtained a resolution while the promise was already rejected");
      },
      function onReject(reason) {
        do_check_eq(reason, 1, "Rejection chose the first value");
      }
    );

    return result;
  }));

// Test that returning normally from a rejection recovers from the error
// and that listeners are informed of a success.
tests.push(
  make_promise_test(function recovery(test) {
    let boom = new Error("Boom!");
    let deferred = Promise.defer();
    const RESULT = "An arbitrary value";

    let promise = deferred.promise.then(
      function onResolve() {
        do_throw("A rejected promise should not resolve");
      },
      function onReject(reason) {
        do_check_true(reason == boom, "Promise was rejected with the correct error");
        return RESULT;
      }
    );

    promise = promise.then(
      function onResolve(value) {
        do_check_eq(value, RESULT, "Promise was recovered with the correct value");
      }
    );

    deferred.reject(boom);
    return promise;
  }));

// Test that returning a resolved promise from a onReject causes a resolution
// (recovering from the error) and that returning a rejected promise
// from a onResolve listener causes a rejection (raising an error).
tests.push(
  make_promise_test(function recovery_with_promise(test) {
    let boom = new Error("Arbitrary error");
    let deferred = Promise.defer();
    const RESULT = "An arbitrary value";
    const boom2 = new Error("Another arbitrary error");

    // return a resolved promise from a onReject listener
    let promise = deferred.promise.then(
      function onResolve() {
        do_throw("A rejected promise should not resolve");
      },
      function onReject(reason) {
        do_check_true(reason == boom, "Promise was rejected with the correct error");
        return Promise.resolve(RESULT);
      }
    );

    // return a rejected promise from a onResolve listener
    promise = promise.then(
      function onResolve(value) {
        do_check_eq(value, RESULT, "Promise was recovered with the correct value");
        return Promise.reject(boom2);
      }
    );

    promise = promise.then(
      null,
      function onReject(reason) {
        do_check_eq(reason, boom2, "Rejection was propagated with the correct " +
                "reason, through a promise");
      }
    );

    deferred.reject(boom);
    return promise;
  }));

// Test that we can resolve with promises of promises
tests.push(
  make_promise_test(function test_propagation(test) {
    const RESULT = "Yet another arbitrary value";
    let d1 = Promise.defer();
    let d2 = Promise.defer();
    let d3 = Promise.defer();

    d3.resolve(d2.promise);
    d2.resolve(d1.promise);
    d1.resolve(RESULT);

    return d3.promise.then(
      function onSuccess(value) {
        do_check_eq(value, RESULT, "Resolution with a promise eventually yielded "
                + " the correct result");
      }
    );
  }));

// Test sequences of |then|
tests.push(
  make_promise_test(function test_chaining(test) {
    let error_1 = new Error("Error 1");
    let error_2 = new Error("Error 2");
    let result_1 = "First result";
    let result_2 = "Second result";
    let result_3 = "Third result";

    let source = Promise.defer();

    let promise = source.promise.then().then();

    source.resolve(result_1);

    // Check that result_1 is correctly propagated
    promise = promise.then(
      function onSuccess(result) {
        do_check_eq(result, result_1, "Result was propagated correctly through " +
                " several applications of |then|");
        return result_2;
      }
    );

    // Check that returning from the promise produces a resolution
    promise = promise.then(
      null,
      function onReject() {
        do_throw("Incorrect rejection");
      }
    );

    // ... and that the check did not alter the value
    promise = promise.then(
      function onResolve(value) {
        do_check_eq(value, result_2, "Result was propagated correctly once again");
      }
    );

    // Now the same kind of tests for rejections
    promise = promise.then(
      function onResolve() {
        throw error_1;
      }
    );

    promise = promise.then(
      function onResolve() {
        do_throw("Incorrect resolution: the exception should have caused a rejection");
      }
    );

    promise = promise.then(
      null,
      function onReject(reason) {
        do_check_true(reason == error_1, "Reason was propagated correctly");
        throw error_2;
      }
    );

    promise = promise.then(
      null,
      function onReject(reason) {
        do_check_true(reason == error_2, "Throwing an error altered the reason " +
            "as expected");
        return result_3;
      }
    );

    promise = promise.then(
      function onResolve(result) {
        do_check_eq(result, result_3, "Error was correctly recovered");
      }
    );

    return promise;
  }));

// Test that resolving with a rejected promise actually rejects
tests.push(
  make_promise_test(function resolve_to_rejected(test) {
    let source = Promise.defer();
    let error = new Error("Boom");

    let promise = source.promise.then(
      function onResolve() {
        do_throw("Incorrect call to onResolve listener");
      },
      function onReject(reason) {
        do_check_eq(reason, error, "Rejection lead to the expected reason");
      }
    );

    source.resolve(Promise.reject(error));

    return promise;
  }));

// Test that Promise.resolve resolves as expected
tests.push(
  make_promise_test(function test_resolve(test) {
    const RESULT = "arbitrary value";
    let promise = Promise.resolve(RESULT).then(
      function onResolve(result) {
        do_check_eq(result, RESULT, "Promise.resolve propagated the correct result");
      }
    );
    return promise;
  }));

function run_test()
{
  do_test_pending();
  run_promise_tests(tests, do_test_finished);
}
