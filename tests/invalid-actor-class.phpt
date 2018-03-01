--TEST--
Actor class instance without implementing Actor::recieve
--DESCRIPTION--
When an Actor class instance is created, if no Actor::recieve method
is implemented, then a fatal error and a segfault is triggered.
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$actorSystem = new ActorSystem();
class Test extends Actor {}
new ActorRef(Test::class, [], 'test');
--EXPECTF--
Fatal error: Class Test contains 1 abstract method and must therefore be declared abstract or implement the remaining methods (phactor\Actor::receive) in %s on line %d
