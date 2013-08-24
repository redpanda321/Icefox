function test_seek8(v, seekTime, is, ok, finish) {

var completed = false;

function startTest() {
  v.currentTime = 1000;
}

function seekEnded() {
  if (completed)
    return false;

  ok(Math.abs(v.currentTime - v.duration) < 0.2,
     "currentTime " + v.currentTime + " close to " + v.duration);
  finish();
}

v.addEventListener("loadedmetadata", startTest, false);
v.addEventListener("seeked", seekEnded, false);

}
