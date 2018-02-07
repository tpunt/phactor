--TEST--
Multiple actor systems
--DESCRIPTION--
Ensure that this is prevented.
--FILE--
<?php

use phactor\ActorSystem;

$actorSystem = new ActorSystem();
$actorSystem = new ActorSystem();
--EXPECTF--
Fatal error: The actor system has already been created in %s on line %d
