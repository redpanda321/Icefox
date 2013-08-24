[mozrunner](https://github.com/mozilla/mozbase/tree/master/mozrunner)
is a [python package](http://pypi.python.org/pypi/mozrunner)
which handles running of Mozilla applications.
mozrunner utilizes [mozprofile](https://github.com/mozilla/mozbase/tree/master/mozprofile)
for managing application profiles
and [mozprocess](https://github.com/mozilla/mozbase/tree/master/mozprocess) for robust process control.

mozrunner may be used from the command line or programmatically as an API.


# Command Line Usage

The `mozrunner` command will launch the application (specified by
`--app`) from a binary specified with `-b` or as located on the `PATH`.

mozrunner takes the command line options from 
[mozprofile](https://github.com/mozilla/mozbase/tree/master/mozprofile) for constructing the profile to be used by 
the application.

Run `mozrunner --help` for detailed information on the command line
program.


# API Usage

mozrunner features a base class, 
[mozrunner.runner.Runner](https://github.com/mozilla/mozbase/blob/master/mozrunner/mozrunner/runner.py) 
which is an integration layer API for interfacing with Mozilla applications.

mozrunner also exposes two application specific classes,
`FirefoxRunner` and `ThunderbirdRunner` which record the binary names
necessary for the `Runner` class to find them on the system.

Example API usage:

    from mozrunner import FirefoxRunner
	
    # start Firefox on a new profile
    runner = FirefoxRunner()
    runner.start()

See also a comparable implementation for [selenium](http://seleniumhq.org/): 
http://code.google.com/p/selenium/source/browse/trunk/py/selenium/webdriver/firefox/firefox_binary.py