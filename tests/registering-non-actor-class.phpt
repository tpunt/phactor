--TEST--
Registering a non-Actor class
--DESCRIPTION--
Ensure that only Actor classes can be used to register a new actor
--FILE--
<?php

$actorSystem = new ActorSystem();

register('a', StdClass::class);

$actorSystem->block();
--EXPECTF--
Warning: register() expects parameter 2 to be a class name derived from Actor, 'StdClass' given in %s on line %d
