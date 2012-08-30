					 ****************************
						    OCARINA
					******************************
					  Written by Federico Commisso
		    				
	##############################################################################################


***** Platforms
  	I have personally compiled and tested this package on the following platforms:
  	---- i486-Linux-gnu, g++ 4.4.3 (Ubuntu 4.4.3-4ubuntu5)

***** Package files
	--- README.txt			Development information
	--- GUIDE.txt			User instructions for compilation and execution
	--- Project3.c			Source Code
	--- Makefile			Compilation dependables make file
	
***** Highlights and idioms
Internal Commands:

	-- ls	<option>		list files in current directory

	-- cd	<path>			change to a different directory given by <path>

	-- clear			clear screen.

	-- set <var> <value>		set shell variable <var> to value given if no var or 
					value is given, list all current variables with their 
					values, one per line.

	-- exit				Exit out of Fed Shell.

	-- jobs				list the current set of processes that have been started 
					by the shell, along with their corresponding commandline

	-- fg <spid>			Move a job from the background to the foreground

***** Known Bugs

	-- 
	
	
***** Revision History

Version 0.3 - Dec 2011
	- Added process management (background/foreground)

Version 0.2 - Oct 2011
	- Added pipeline
	- I/O redirection is now handled

Version 0.1 - Sep 2011
	- Compiles with g++ 4.2-4.4
	- First release - incorporated various internal shell commands as 
	  well as the ability to search for executable commands.
