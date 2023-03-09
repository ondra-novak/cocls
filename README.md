# Knihovna COCLS - implementace generátorů a asynchroních operací pomocí korutin

Knihovna COCLS poskytuje stavební třídy a funkce pro podporu generátorů, asynchornních operací, plánování a synchronizaci korutin.

**POZNÁMKA:** Všechny symboly v této dokumentaci jsou uváděny bez namespace. 

```
using namespace cocls;
```

## Typ korutiny pro asynchroní operace

```
#include <cocls/async.h>
```
Korutina `async` představuje korutinu, která může být přerušena pomocí operátoru `co_await`. K vracení hodnoty pak použije `co_return`

```
async<T> coroutine(Args ... args) {
   //
   co_await ...;
   //
   co_await ...;
   //
   co_return ...;
}
```

**POZOR!** - korutina se zavoláním sama nespustí. Je potřeba ji spustit buď přímo, nebo pomocí dalších tříd - viz dále. 

Zavoláním korutiny získáme objekt `async<T>`. Tento objekt lze přesouvat, ale nelze jej kopírovat. Zůstává platný dokud není korutina spuštěna - spuštěním korutiny je ekvivalentní operace jako přesun instance korutiny do interní částy knihovny.

### Spouštění korutiny - detached režim

Korutinu `async<T>` lze spustit v detached režimu. V tomto režimu se korutina spustí aniž by bylo možné získat informaci o ukončení běhu, ani výsledek korutiny (detached)

```
void async<T>::detach()
```


```
async<int> coro = coroutine(... args...);
coro.detach(); //spuštění v detached režimu
```

Jakmile je korutina spuštěna, proměnná `coro` může být zničena. Zničení nespuštěné korutiny je povolená operace s tím, že dojde k vyvolání destruktorů uložených parametrů

### Spuštění korutiny - nastavení promisy výsledkem

Korutinu `async<T>` lze svázat s existujícím objektem `promise<T>` (viz dále), pak se korutina spustí a po dokončení se výsledkem vyplní svázana promise.

```
void async<T>::start(promise<T> &p);
```

```
promise<int> p =...;
async<int> coro = coroutine(... args...);
coro.start(p);
```

V příkladu nahoře po této operace budou obě proměnné, jako `coro` tak `p` prázdné a mohou být zničeny. Korutina nyní poběží nezávisle na kodu a jakmile dokončí, nastaví připojenou promisu.

### Spuštění korutiny - vrácení future<T>

Korutinu `async<T>` lze spustit tak, že výsledkem je objekt `future<T>` (viz dále). Tento objekt lze použít k čekání na výsledek korutiny. 

```
future<T> async<T>::start();
```

```
async<int> coro = coroutine(... args...);
future<int> f = coro.start();
//...
int result = co_await f;
```

Pokud má být korutina spuštěna synchroně (s čekáním na dokončení) v rámci běžného kódu (ne v korutině), lze použít f.wait();

```
async<int> coro = coroutine(... args...);
future<int> f = coro.start();
//...
int result = f.wait();
```

Zkrácené zápisy

```
//asynchroně
int result = co_await coroutine(... args...).start();
```

```
//synchroně
int result = coroutine(... args...).start().wait();
```


Místo volání funkce start(), lze korutinu spustit také konstrukcí future<T> přímo výsledkem korutiny

```
//korutina se spustí konstrukcí proměnné f,
future<int> f = coroutine(... args...);

int result = co_await f;
```

## Třídy future<T> a promise<T>

```
#include <cocls/future.h>
```

Tyto třídy jsou **základním synchronizačním nástrojem** pro komunikaci mezi korutinami a zbytkem kódu. 

* **future<T>** - představuje budoucí proměnnou daného typu. Hodnota je nastavena později. Na okamžik nastavení hodnoty lze čekat asynchronně pomocí `co_await` nebo synchroně pomocí metody `wait()` případně `sync()`. **POZOR!** Objekt **nelze ani kopírovat ani přesouvat**. Lze jej však vracet z funkce. Sdílený stav **není alokován na heapu**.

* **promise<T>** - představuje objekt svázaný s instancí future<T>. Slouží k nastavení hodnoty svázané `future<T>`. Objekt nelze kopírovat, ale lze jej přesouvat. Nastavit hodnotu lze pouze jednou, poté to již není možné, nastavením hodnoty dojde atomicky k rozpojení svazku. Tato operace je MT safe, více vláken se může pokoušet nastavit promise<T>, pouze první uspějě. Objekt má vlastnosti `callable`. Je tedy možné vložit objekt do `std::function`

### Konstrukce future<T> a promise

```
future<int> f([&](promise<int> promise) {
    do_something_with_promise(std::move(promise));
});
int result = co_await f;
```

### Konstrukce future<T> vrácené z funkce

Toto je nejčastější způsob konstrukce future<T> pokud má být vrácena a přitom nejde  o korutinu. Může se jednat o napojení future do systému založeným na callback funkcích.

```
future<int> retrieve_value() {
    return [&](auto promise) {
        listener.register_callbacak(std::move(promise));
        listener.request_value();
    };
}
```

Výše uvedený příklad předpokládá, že `listener` je nějaký asynchroně řízení naslouchač dat. Nejprve tedy zaregistrujeme promisu (může být zaregistrován jako callback) a následně požádáme listener o získání hodnoty. Tato operace může proběhnout asynchroně, tedy ihned po žádosti nemusí být hodnota k dispozici a kód pokračuje návratem z funkce. K nastavení hodnoty dojde k zavolámím `promise` s hodnotou

```
promise(<value>)
```

### Konstrukce future<T> korutinou.

Stačí do konstruktoru vložit `async<T>` a dojde ke spuštění korutiny, jejiž výsledek nastaví future

```
future<int> f(coroutine(...args...));
```

```
future<int> run_async(...args...) {
    return coroutine(...args...);
}
```

### Konstrukce návratové hodnoty future<T> korutinou přímo

Třída future<T> nabízí zkratku pro psaní korutin. Funkce která vrací `future<T>` může ve svém těle použít `co_await` a `co_retunr` aniž by si překladač stežoval, protože třída `future` referencuje typ async<T> pro systém korutin

```
future<int> coroutine_example() {
    co_await...; //
    
    co_return
}
```

Takto napsaná korutina vychází z `async<T>`, je automaticky spuštěna, pokud je zavolána a vrácena `future<T>` je nastavena výsledkem korutiny

Tato zkratka má určité drobné nevýhody. Zatímco objekt `async<T>` lze přesouvat, objekt `future<T>` nikoliv, Typ `async<T>` lze také použít s alokátorem (viz dále), samotné `future<T>` takto použít nejde. Pokud je tohle problém, lze výše zmíněnou zkratku přepsat na dvě funkce


```
future<int> coroutine_example(args) {
    return coroutine_example_coro(args);
}
async<int> coroutine_example_coro(args) {
    co_await...; //
    
    co_return...;
}
```

### Příjem výsledku future<T> do existující instance

Třída future<T> není přesouvatelná ani kopírovatelná a to proto, že adresa proměnné se nesmí změnit po dobu, kdy čeká na nastavení hodnoty. Nelze ani použít přiřazení protože tento operátor vyžaduje kopii nebo přesun. Třída definuje operátor <<, kterým lze převzít výsledek volání funkce, která vrací future<T>.

```
future<int> coroutine_example();


future<int> val;
val << [&]{return coroutine_example();};
int result = co_await val;
//lze opakovaně
val << [&]{return coroutine_example();};
int result = co_await val;
```

Operátor << vyžaduje na pravé straně funkci, která vrací future<T>. V rámci zpracování je funkce zavolána a výsledná future je zkonstruovaná v instanci na levé straně operátoru. Pokud je třeba zavolat funkci speciálním způsobem (metoda, parametry), lze volání zabalit do lambda funkce, nebo použít std::bind.

### Zahození výsledku future<T>

Třída future<T> je označena `[[nodiscard]]` záměrně, protože nesmí být zahozena před nastavením hodnoty. Zahození proměnné před nastavením hodnoty je považováno za chybu, tato chyba se oznámí pomocí `assert()`, a v release je to UB.

Pokud je přesto třeba výsledek zahodit, je třeba použít funkci  `cocls::discard()`. Tato funkce očekává funkci, která vrací  `future<T>` jenž má být zahozena

```
discard([&]{return coroutine_example();}
```

Funkce `discard()` alokuje future<T> na haldě a v okamžiku nastavení hodnoty ji zničí. Tuto funkci použijte, pokud nemáte jinou možnost jak zahodit výsledek volání. Pokud je volání
ve formě korutiny, pak je lepší použít async<T>::detach(), která též zahazuje výsledek korutiny a přitom nepotřebuje alokovat speciální místo na haldě.

### Objekt shared_future<T>

```
#include <cocls/shared_future.h>
```

Třída `shared_future<T>` je na rozdíl od `future<T>` objekt, který lze kopírovat nebo přesouvat. Těmito operacemi se sdílí vnitřní stav, který je alokován na heapu. Více korutin může čekat na tento typ future.

Tato future je sdílena podobně jako shared_ptr<T>. Pokud je objekt ve stavu `pending` (čeká na hodnotu), je přiznaná jedna reference navíc, což umožňuje sdílenou future zahodit, tedy zrušit všechny reference aniž by to způsobilo chybu nebo UB. Sdíleny stav bude existovat dokud se výsledek nenastaví.


Instanci `shared_future<T>` konstruujeme pomocí argumentů podobně jako `discard`. Do konstruktoru musíme vložit funkci, která vrací `future<T>`. Konstruktor si funkci zavolá a nabídne sdílené místo pro návratovou hodnotu.

```
shared_future<int> f([&]{return coroutine_example();});
int result = co_await f;


```

### Dropnutí promise

Promise by měla být nakonec nastavena. Avšak je možné promisu zničit bez nastavení hodnoty. Tomuto stavu se říká "drop". Pokud je promisa dropnuta, není nikdo, kdo by čekající futuru nastavil. V takovém případě je do future nastaven stav "bez hodnoty". Při pokusu načíst tuto hodnotu se pak výhodí výjimka na straně čekající korutiny

Promise může být dropnuta i ručně pomocí funkce `drop()`

### Nastavení výjimku

Promise nemusí být jen použita k nastavení hodnoty, ale k nahlášení výjimky. Pokud je promise zavolána s objektem `std::exception_ptr`, pak se uložená výjimka vyhodí na čekající straně.

```
    try {
        //operation
        p(calc());
    } catch (...) {
        p(std::current_exception());
    }
```




### Další operace s future<T>

* **synchronní čekání** - .`wait()` - blokne vlákno dokud se nenastaví výsledek, pak vrací výsledek
* **synchronizace bez vyzvednutí hodnoty** - `sync()` - Tato funkce pouze blokne vlákno, dokud se hodnota nenastaví. Ale na hodnotu nepřistupuje, takže nevyhazuje ani výjimku, pokud byla nastavena místo hodnoty
* **detekce stavy "bez hodnoty" - `has_value()` - tato funkce testuje, zda future byla nastavena hodnotou nebo výjimkou. Pak vrací `true`. Pokud byla nastavena bez hodnoty, vrátí `false`. Pokud future ještě nastavena nebyla, pak udělá sync(). Tuto funkci lze zavolat i přes `co_await` ve formě (`co_await f.has_value()`), pak se provede čekání s uspáním korutiny a funkce vrací true/false podle výsledku - přičemž nevyhazuje výjimku.



## Řízení běhu korutin

### Dva stavy vlákna

V rámci korutiny async<T> existuje evidence dvou stavů vlákna
* **normální běh (normal mode)** - ve kterém jsou voláný funkce, ne však korutiny
* **běh v korutině (coro mode)** - v tomto stavu je v zásobníku aktivní aspoň jeden rámec patřící korutině. 

**Poznámka** - režim je evidován jen v rámci objektů z knihovny `cocls`. Pokud dojde k aktivaci rámce patřící jiné knihovně, nemusí detekce tohoto rámce fungovat. Kdykoliv je spuštěna, nebo obnovena korutina v *normal mode*, přejde vlákno do stavu *coro mode* a tento stav opustí v okamžiku, kdy se tato korutina ukončí nebo přeruší.

### Řízení korutin v *coro mode*

Pokud jakákoliv korutina má být obnovena v režímu *coro mode*, nedojde k jejímu okamžitému obnovení. Místo toho je korutina zařazena do fronty k obnovení. Tato fronta existuje v rámci vlákna (každé vlákno má oddělenou frontu). Ukončení nebo přerušení aktuálně běžící korutiny obnoví další korutinu z fronty. Režim *coro mode* se ukončí, jakmile je fronta prázdná

Pokud Váš kód řídí korutiny manuálně a chcete využít tuto vlastnost, nevolejte `h.resume()`, místo toho zavolejte `coro_queue::resume(h)`. Tuto funkci lze bezpečně volat i v *normal mode*, pak se vlákno přepne do *coro mode* podle výše uvedených pravidel.

**Poznámka** - odlišení *coro mode* od *normal mode* řeší situace, ve kterých by mohlo dojít k naskládání rámců korutin na aktuálním zásobníku. Navíc pokud by došlo k pokusu obnovení korutiny, která již má svůj rámec aktivní, je toto považováno za chybu a výsledkem je UB. Obnovování korutin v režimu *coro mode* znemožňuje naskládání rámci na sebe

**Výjimka** - některé objekty vyžadují použití volání přímého `h.resume()`, pak k částečnému naskládání dojít může. Typicky pokud se používá synchroní přístup v rámci korutiny, například synchronní generátor, nebo `signal::collector`

### Využití symmetrického transferu

Použití fronty umožňuje v některých situacích využít symmetrický transfer. To znamená, že když jedna korutina je uspána, může jiná korutina, připravná ve frontě, být obnovena v rámci jedné operace. Překladače často umí takto rychle přepínat mezi korutinami.

### Funkce pause()

Funkce pause() uspí aktuální korutinu a obnoví první korutinu připravenou ve frontě. Současná korutina je zároveň vložena do fronty aby byla obnovena příště

```
co_await pause();
```

Pokud v aktuálním vlákně není připravena žádná korutina k běhu, pak funkce nedělá nic. Již řazené korutiny se obnovuji v pořadí zařazení. Pokud víc korutin opakovaně používá tuto funkci, pak se exekuce střídá v pořadí jejich prvotního zařazení (fronta)

### Ruční přepnutí do korutiny pomocí promise<T>

Třída promise<T> umožňuje obnovit korutinu, která čeká na výsledek future<T> k níž je svázaná promise.

```
promise<int> p = ....;

co_await switch_to(p,value);
```

Funkce switch_to musí být zavolána v korutině v příkazem `co_await`. V rámci operace nastaví danou promise, uspí aktuální korutinu a zařadí jí do fronty (jako pause()) a obnoví čekající korutinu, která si okamžitě může načíst hodnotu. Řízení je předáno pomocí symmetrického transferu. Pokud je obnovena korutina zase uspána nebo ukončena, je současná korutina obnovena v závislosti na stavu fronty.


## Generátor

```
#include <cocls/generator>
```

Generátor je třída, která tvoří základ korutiny, ve které lze používat `co_yield`.

```
generator<T, Arg> gen_coroutine(...args...) {
    co_yield...;
    co_yield...;
    co_return;
}
```

* **T** - typ hodnoty, který generátor posíla zkrze `co_yield` ven z generátoru (generátor pak tento typ vrací při každém zavolání) - nesmí být `void`
* **Arg** - typ hodnoty, která vstupuje do generátoru při volání generátoru, a vystupuje jako výsledek operace `co_yield`. Může být `void` (výchozí), pak generátor nevyžaduje žádný argument při každé iteraci

### Vlasnosti objektu

* **nelze kopírovat** - objekt generátoru nelze kopírovat
* **lze přesouvat** - objekt generátoru lze přesouvat
* **lze předčaně destruovat** - pokud generátor zrovna nepracuje, čeká na další zavolání, lze generátor zničit, aniž by to způsobilo problémy a to i v případě, že generátor byl přerušen uprostřed vypočtu (musí čekat na `co_yield`). Zajistěte si správné volání destruktorů, všechny objekty musí splňovat RAII. Samotný akt destrukce lze zachytit pouze destruktorem.


### Korutina

Korutina generátoru vždy startuje v zastaveném stavu a je probuzena při prvním zavolání generátoru. Poté je přerušena na co_yield a při dalším zavolání opět probuzena a pokračuje v činnosti za co_yield

Korutina může používat co_await (viz dále)

### Různé typy generátorů

* **synchronní** - mezi co_yield nedochází k jinému přerušení (žádný co_await)
* **asynchronní** - mezi co_yield používá generátor co_await

### Způsoby volání generátoru

* **synchornní** - volající počká, dokud není vygenerovaný výsledek. Pokud se jedná o asynchornní generátor, zahrnuje to blokující operaci
* **asynchronní** - volání generátoru vyžaduje použití `co_await`. V takovém případě je volající korutina uspána a teprve až generátor vygeneruje hodnoty, je korutina probuzena. V tomto režimu nezáleží na tom, jestli je genrátor synchronní nebo asynchronní.
* **iterátorem** - umožňuje používat standardní iterátory pro iteraci výsledků. Generátor lze použít v range_for. Tento přístup je implicitně synchornní
* **vrací future** - tímto způsobem se zavoláním generátoru vrátí future, na kterou je třeba počkat

### Počet cyklů

* **omezený** - generátor má omezený počet cyklů a pak skončí. Volající musí být schopen detekovat, že byla vygenerovaná poslední hodnota
* **nekonečný** - generátor má schopnost generovat nekonečně hodnot. Generátor lze ukončit pouze zničením instance.

### API - next() a value()

#### Synchronní přístup

```
generator<int> gen = ....;
while (gen.next()) {
    print(gen.value());
}
```

S každým dalším zavoláním `.next()` se vygeneruje hodnota. Funkce vrací `true` pokud hodnota
byla vygenerovaná, nebo false, pokud generátor skončil

Hodnota je dostupná přes `.value()`. Je vrácena jako reference. Tato reference je platná do dalšího `.next()`

#### Asynchronní přístup

```
generator<int> gen = ....;
while (co_await gen.next()) {
    print(gen.value());
}
```

### API - iterátory
```
generator<int> gen = ....;
auto iter = gen.begin();
while (iter != gen.end()) {
    print(*iter);
    ++iter;
}
```

Ranged_for:

```
generator<int> gen = ....;
for(auto &val: gen) {
    print(val);
}
```

### API - future

V tomto případě generátor voláme jako funkci

```
generator<int> gen = ....;
while(true) {
    future<int> val = gen();
    if (val.has_value()) {
        print(*val);
    } else {
        break;
    }
}
```

S použitím operátoru <<


```
generator<int> gen = ....;
future<int> val = gen();
while(val.has_value()) {
    print(*val);
    val << gen;
}
```

Asynchronní přístup

```
generator<int> gen = ....;
future<int> val = gen();
while(co_await val.has_value()) {
    print(*val);
    val << gen;
}
```

### Generátor s argumentem

Pokud je **Arg** jiný než **void**, pak tento argument se nastavuje při volání funkce `next()`

```
generator<int, int> gen = ....;
int counter = 1;
while (gen.next(counter)) {
    print(gen.value());
    ++counter;
}
```

V rámci korutiny je pak hodnota k dispozici přes

```
int val = co_yield <expr>;
```

#### Přístup k první hodnotě argumentu. 

Při prvním zavolání generátoru s argumentem není zaslaná hodnota přímo k dispozici. Při zavolání `co_yield` se očekává již nějaký výsledek a pak by se načetla druhá zaslaná hodnota argumentu. Proto lze v tomto případě použít zápis `co_yield nullptr`;

```
generator<int,int> doubler() {
    //retrieve very first argument
    int arg = co_yield nullptr;
    while (true) {
        arg = co_yield arg * 2; //double every argument and return it as result
    }
}

void print_gen() {
    auto gen = doubler();
    for (int i = 0; i < 10; i++) {
        auto v = doubler(i);
        print(*v); //*v is shortcut to v.wait();
    }
}
```

### Generátor aggregator

Víc generátorů lze agregovat do jednoho generátoru. Pokud se jedná o synchroní generátory, pak agregace způsobí, že jednotlivé generátory se budou střídat v generování další hodnoty. Pokud se jedná o asynchroní generátory, pak agregovaný generátor vždy vygeneruje hodnotu 
toho generátoru, který jako první dokončil svůj generační proces. Další generátory se řadí do fronty,

```
generator<T,Arg> generator_aggregator(std::vector<generator<T,Arg> gens);
```

Pozor na to, že generátory nejsou kopírovatelné. Vektor připravených generátorů je tedy nutné poslat přes std::move()

Výsledkem operace je jeden generátor, který lze použít standardním způsobem. Generátor je synchronní, pokud všechny generátory v agregaci jsou synchroní. Jeden asynchroní generátor způsobí, že generátor je asynchorní.


Pokud se agregují generátory s argumentem, tak první zavolání agregátoru způsobí inicializaci všech generátorů v agregaci stejným argumentem. Každé další zavolání způsobí, že argument si převezme generátor, jehož hodnota byla vrácena předchozím voláním.



## Zámek (mutex)

Korutiny mají limitované možnosti používat zámky. Není možné držet zámek (`std::mutex` a varianty) mezi co_await, pokud hrozí, že bude exekuce přestěhovaná do jiného vlákna. Navíc držení zámku počas uspání není bezpečné, může dojít k deadlocku.

Aby bylo možné v rámci korutin zámky používat tak, aby zámek neblokoval vlákno, pouze čekající korutinu, existuje třída `mutex` v rámci knihovny `cocls`

```
cocls::mutex mx;

async<void> do_something() {
    auto ownership = co_await mx.lock();
    //nyní vlastníme mutex mx
    co_await ...;
    co_await ...;
    co_return;
    //zámek se uvolní zničením objektu ownership
}
```

Narozdíl od standardního mutexu, zde se funkce `lock()` musí volat s `co_await`. Pokud je zámek vlastněn jiným vlastníkem, pak se korutina uspí a je probuzena jakmile se vlastnictví
mutexu uvolní. Vlastnictví mutexu se sleduje objektem `ownership`. Tento objekt není kopírovatelný, pouze přesouvatelný. Jakmile je objekt opuštěn, nebo je zavolána funkce `.release()`, pak je zámek odemčen.

Objekt `mutex` implementuje frontu čekajících korutin. Pokud některá korutina uvolní mutex,
vlastnictví se automaticky přenese na první čekající korutinu. Vlastní obnovení korutiny se řídí podle pravidle vlákna ve stavu *coro mode*. Tedy obnovená korutina je umístěna do fronty k obnovení a je obnovena podle plánu jakmile se aktuální korutina dokončí nebo přeruší. Stále běžící korutina může tento proces urychlit použítím funkce `pause()`

Uvolnění mutexu předčasně


```
cocls::mutex mx;

async<void> do_something() {
    auto ownership = co_await mx.lock();
    //nyní vlastníme mutex mx
    co_await ...;
    ownership.release();
    //zámek se uvolní zničením objektu ownership
    //nový vlastník ale není obnoven
    long_op();
    co_return;
    //až tady se ke slovu dostane nový vlastník mutext
}
```

Urychlení obnovení čekající korutiny

```
cocls::mutex mx;

async<void> do_something() {
    auto ownership = co_await mx.lock();
    //nyní vlastníme mutex mx
    co_await ...;
    ownership.release();
    //zámek se uvolní zničením objektu ownership
    //uvolni vlákno novému vlastníkovi
    co_await pause();    
    long_op();
    co_return;
}
```
### Další operace mutexu

* **mutex::try_lock()** - lze volat i v *normal mode*, neblokuje
* **mutex::lock().wait()** - zamkne mutex v *normal mode*, blokující volání


## Fronta (queue)

```
#include <cocls/queue.h>
```

Fronta existuje ve dvou variantách `queue<T>` a `limited_queue<T>`

Fronta umožňuje nechat korutiny reagovat na hodnoty vkládané do fronty

* Pokud je fronta prázdná, pak operace pop() způsobí, že aktuální korutina je uspaná a je následně probuze první vloženou hodnotou
* Pokud fronta není prázdné, pak operace pop() korutinu neuspí a je ihned vybrána první hodnota z fronty
* Pokud je fronta limited_queue plná, pak korutina volající operaci  push() je uspána a je následně probuzena pokud někdo vyzvedne první hodnotu z fronty a uvolní v ní místo.

**Poznámka** - funkce pop() a funkce push() (u limited-queue) vrací future<T>.

```
async<void> read_queue(queue<int> &q) {
    while(true) {
        int val = co_await q.pop();
        print(val);
    }
}

void fill_queue(queue<int> &q) {
    q.push(10);
}
```

### Odblokování čekajícího.

Pokud korutina čeká na funkci .pop(), lze ji odblokovat a zaslat ji výjimku.

```
void stop_reader(queue<int> &q) {
    q.unblock_pop(std::make_exception_ptr(await_canceled_exception()));;
}
```
**Pozor:** Pokud ve frontě nikdo nečeká, nebo je fronta plná neodebraných hodnot, k odblokaci
nedojde

Typické použití je k implementaci timeoutu. Koroutina, která čte si nainstaluje timer, který zavolá `unblock_pop`, pokud vyprší čas čekání na hodnotu ve frontě. Pakliže je hodnota získána, může být timer odinstalován. 

