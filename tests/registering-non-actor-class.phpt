--TEST--
Registering a non-Actor class
--DESCRIPTION--
Ensure that only Actor classes can be used to spawn a new actor
--FILE--
<?php

use phactor\{ActorSystem, function spawn};

$actorSystem = new ActorSystem();

spawn('a', StdClass::class);
--EXPECTF--
Warning: phactor\spawn() expects parameter 2 to be a class name derived from phactor\Actor, 'StdClass' given in %s on line %d
