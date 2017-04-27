--TEST--
Property visibility
--DESCRIPTION--
Ensure that visibility of properties for Actor-based objects is still enforced.
--FILE--
<?php

$actorSystem = new ActorSystem();

class Test extends Actor
{
    public $a = 1;
    protected $b = 2;
    private $c = 3;
    private $d = 4;

    public function __construct()
    {
        var_dump($this->a, $this->b, $this->c);
    }

    public function receive($a, $b){}
}

class Test2 extends Test
{
    private $d = 4;

    public function __construct()
    {
        var_dump($this->a, $this->b);

        try {
            var_dump($this->c);
        } catch (TypeError $te) {
            echo $te->getMessage();
        }

        var_dump($this->d);
    }
}

$test = new Test();
$test2 = new Test2();

var_dump($test->a);

try {
    var_dump($test->b);
} catch (TypeError $te) {
    echo $te->getMessage();
}

try {
    var_dump($test->c);
} catch (TypeError $te) {
    echo $te->getMessage();
}

$actorSystem->block();
--EXPECT--
int(1)
int(2)
int(3)
int(1)
int(2)
Cannot read property 'c' becaused it is private
int(4)
int(1)
Cannot read property 'b' becaused it is protected
Cannot read property 'c' becaused it is private
