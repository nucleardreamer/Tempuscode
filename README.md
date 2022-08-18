Welcome to the Tempus code!

Requirements
------------------------------------------------------------------------
Tempus currently uses libpq, libxml2, and GLib.  All of these must be
installed for tempus to build.  In addition, if you wish to do more
than minor development, you should have the GNU autoconf system
installed.

In Debian or Ubuntu, you should be able to install all the necessary
prerequisites with this command:

aptitude install build-essential postgresql autotools-dev autoconf \
                 libpq-dev libxml2-dev libglib2.0-dev check git \
                 autoconf-archive

Getting Tempus Code
------------------------------------------------------------------------
Tempus development has been through many different source control
systems in its lifetime.  It started using CVS in 1999, then switched
to Subversion in 2002.  The repository was converted to darcs in 2007,
and then to git in 2009.  It was placed on github in 2011.

You should be able to get the latest copy with the following:

  git clone git://github.com/TempusMUD/Tempuscode.git

Building Tempus
------------------------------------------------------------------------
Tempus uses the autoconf system to generate its configure script, so
there are a few things you should do to maintain your sanity.

If you've just checked the code out of the repository, run from bash:

$ sh bootstrap

This will generate the configure script and all the necessary makefile
templates.  Then, run the following to configure your system:

  cd build
  ../configure

This will ensure that intermediate build files are placed in the build
directory rather than the source directory (which can get very messy).
There is a makefile in the root tempus directory which will forward its
make requests to the makefiles in the build directory.

If you add any source files to the system, you must run the bootstrap
script again.  Don't forget to add them to your repository!

If you have a multiprocessor machine, you can build Tempus much more
quickly by running this command first:

  export MAKEFLAGS='-j #'

where # is the number of processors (or cores) you have + 1.  Dual-core
machines should put 3, quad-cpu machines should put 5, etc.  This
ensures the maximum use of all your processors.  This is a good
command to put in your .profile.


Configuring Postgresql
------------------------------------------------------------------------
Tempuscode uses both the lib/ directory and a postgresql database to
store its data.  The postgreql database must be set up correctly for
Tempuscode to run.

```
   sed -i "/^# TYPE / a\local   devtempus   realm    trust" /var/lib/postgresql/data/pg_hba.conf
   
   /etc/init.d/postgresql reload # since this is containerized, restart the container instead, keeping command there for posterity
   
   su - postgres -c 'createuser -h 0.0.0.0 -d -R -S realm' # confirm if this is just for windows docker...?
   
   su - postgres -c 'createdb -h 0.0.0.0 -O realm devtempus' # reminder 0.0.0.0 for windows...
   
   # log out of postgres if you were logged in...
   
   psql -h 0.0.0.0 -U realm -d devtempus -f sample_lib/etc/tempus.sql
```

Running Tempuscode
------------------------------------------------------------------------
CD into the build directory, and then compile:
  make all

CD back out into the Tempuscode directory.
You should now be able to run the code with this command:

  bin/circle sample_lib

Happy hacking!

## Stuff we're still figuring out...

Joining the server...

Windows Docker ...need to find out what localhost actually is...?

*CAN* connect in container

for balena

  change port 4040 -> 80

for aws or digital ocean

  need to configure network settings for players to join...

  need to find out what directory needs to be stored for game data...
