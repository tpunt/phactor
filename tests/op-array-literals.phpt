--TEST--
Ensure op array literals are properly copied
--DESCRIPTION--
Basic values don't need manual copying.
Strings are interned, so also don't need copying.
Arrays do need duplicating.
--FILE--
<?php

$actorSystem = new ActorSystem();

class A
{
    public static function B()
    {
		return 'a';
		return 1.1;
		return 1;
		return null;
		return true;
		return false;
        return [];
		return $a;
    }
}

new class extends Actor {
    public function __construct()
    {
        $this->send($this, 1);
    }

    public function receive($sender, $message)
    {
        A::B();
    }
};

$actorSystem->block();
--EXPECT--