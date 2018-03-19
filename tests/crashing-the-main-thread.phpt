--TEST--
Ensure the actor system shuts down if the main thread crashes
--FILE--
<?php

use phactor\{ActorSystem};

$as = new ActorSystem(1);

throw new exception();
--EXPECTF--
Fatal error: Uncaught Exception in %s:%d
Stack trace:
#0 {main}
  thrown in %s on line %d

Termsig=9
