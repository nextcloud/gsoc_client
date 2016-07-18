FAQ
===

**Issue:**

Some files are continuously uploaded to the server, even when they are not modified.

**Resolution:**

It is possible that another program is changing the modification date of the file.

If the file is uses the ``.eml`` extension, Windows automatically and
continually changes all files, unless you remove
``\HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers``
from the windows registry.

See http://petersteier.wordpress.com/2011/10/22/windows-indexer-changes-modification-dates-of-eml-files/ for more information.


**Issue:**

Synced folders appear with wrong timestamp.

**Resolution:**

On POSIX systems the semantics of timestamps is special. The timestamp is updated, whenever a file is created or removed,
but it is not updated, when file contents or contents of subfolders changes.

We do not sync the timestamps of folders for this reason.
Users are advised to not rely on timestamps of folders.


