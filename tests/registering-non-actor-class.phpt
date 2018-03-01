--TEST--
Registering a non-Actor class
--DESCRIPTION--
Ensure that only Actor classes can be used to spawn a new actor
--FILE--
<?php

use phactor\{ActorSystem, ActorRef};

$actorSystem = new ActorSystem();

new ActorRef(StdClass::class);
ActorSystem::shutdown();
--EXPECTF--
Warning: phactor\ActorRef::__construct() expects parameter 1 to be a class name derived from phactor\Actor, 'StdClass' given in %s on line %d
