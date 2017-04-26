<?php

$actorSystem = new ActorSystem();

class Test extends Actor
{
    public $a = 1;
    protected $b = 2;
    private $c = 3;

    public function __construct()
    {
        var_dump($this->a, $this->b, $this->c);
    }

    public function receive($a, $b){}
}

class Test2 extends Test
{
    public function __construct()
    {
        var_dump($this->a, $this->b);

        try {
            var_dump($this->c);
        } catch (TypeError $te) {
            echo $te->getMessage();
        }
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

/*
class Test extends Actor
{
    private $str = 'abc';
    protected $a = 1;
    public $b;
    // private $ints = [];
    // private $b;

    // const A = 1;
    // const B = 2;

    public function __construct(string $str)
    {
        var_dump($this->str, $this->a, $this->b);
        // $this->a = 1;
        // $this->b = 2;
        // $this->c = 3;
        // $this->d = 4;
        // $this->str = $str;
    }

    public function receive($sender, $msg)
    {
        // var_dump($this);
        // var_dump($this->b);
        // var_dump(self::A, self::B);
        // var_dump($this->t(), $this->a, $this->b, $this->c, $this->d, $this->int);
        // $a = new Test();
        // $this->send($sender, "{$this->str}: $msg ({$this->a})");
        // $this->remove();
    }

    // private function t(){return 10;}
}

$a = new Test('Person 1');
var_dump($a->b, $a->a, $a->str);
new class($a) extends Actor {
    public function __construct(Test $a)
    {
        // var_dump($a);
        // $a->b = 2;
        $this->send($a, "Hit me");
    }

    public function receive($sender, $msg)
    {
        var_dump($msg);
        $this->remove();
    }
};
for ($i = 0; $i < 10000000; ++$i);
$actorSystem->block();
*/

/*
$t = new class(3) extends Thread {
    private $a = 1;
    private $b = 2;

    public function __construct($b)
    {
        $this->b = $b;
    }
};
$t->a = 4;
var_dump($t->a);
*/
