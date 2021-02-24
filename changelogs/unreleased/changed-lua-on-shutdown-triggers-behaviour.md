## feature/core

*  Previously lua on_shutdown triggers were started sequentially,
   and if one of them ended with non-zero status, then the subsequent
   triggers were not started. Now each of triggers starts in a separate
   fiber and then we wait for their completion for 3.0 seconds. In case
   trigger ended with non-zero status we only loggs cause of error.