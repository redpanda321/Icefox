Cu.import("resource://services-sync/util.js");

// ----------------------------------------
// Fake Sample Data
// ----------------------------------------

let fakeSampleLogins = [
  // Fake nsILoginInfo object.
  {hostname: "www.boogle.com",
   formSubmitURL: "http://www.boogle.com/search",
   httpRealm: "",
   username: "",
   password: "",
   usernameField: "test_person",
   passwordField: "test_password"}
];

// ----------------------------------------
// Fake Login Manager
// ----------------------------------------

function FakeLoginManager(fakeLogins) {
  this.fakeLogins = fakeLogins;

  let self = this;

  // Use a fake nsILoginManager object.
  delete Svc.Login;
  Svc.Login = {
      removeAllLogins: function() { self.fakeLogins = []; },
      getAllLogins: function() { return self.fakeLogins; },
      addLogin: function(login) {
        getTestLogger().info("nsILoginManager.addLogin() called " +
                             "with hostname '" + login.hostname + "'.");
        self.fakeLogins.push(login);
      }
  };
}
